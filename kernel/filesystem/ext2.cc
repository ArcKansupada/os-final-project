#include "ext2.h"

// Returns 0 (e.g. ENOSPC) on failure; `added` counts only blocks actually allocated.
sync<uint32_t> provision_block(Node *node, uint32_t targetBlock, uint32_t &added) {
  auto res = co_await node->ext2->provision_block(targetBlock, node->number);
  added += !res.error;
  co_return res.error ? 0 : res.block;
}

sync<uint32_t> Node::get_block(const Ext2Inode& readonly, Mutex<Ext2Inode>::Proof& write, uint32_t number, bool force) {
  uint32_t added_sectors = 0;
  uint32_t offset = 0;
  if (number < 12) // direct
  {
    offset = readonly.direct_block[number];
    if (offset == 0 && force) {
      if (!(offset = write.data().direct_block[number] = co_await provision_block(this, number == 0 ? 0 : readonly.direct_block[number-1], added_sectors))) goto finish;
    }
  } else if ((number -= 12) < ptrPerBlock) // singly indirect
  {
    if (readonly.singly_indirect_block == 0) {
      if (!force) co_return 0; // sparse file
      if (!(write.data().singly_indirect_block = co_await provision_block(this, readonly.direct_block[11], added_sectors))) goto finish;
    }
    co_await device->read(readonly.singly_indirect_block*block_size + number*sizeof(uint32_t), offset);
    if (offset == 0 && force) {
      if (!(offset = co_await provision_block(this, readonly.singly_indirect_block, added_sectors))) goto finish;
      co_await device->write(readonly.singly_indirect_block*block_size + number*sizeof(uint32_t), offset);
    }
  } else if ((number -= ptrPerBlock) < ptrPerBlockSq) // doubly indirect
  {
    uint32_t indirect;
    if (readonly.doubly_indirect_block == 0) {
      if (!force) co_return 0; // sparse file
      if (!(write.data().doubly_indirect_block = co_await provision_block(this, readonly.singly_indirect_block, added_sectors))) goto finish;
    }
    co_await device->read(readonly.doubly_indirect_block*block_size + number/ptrPerBlock*sizeof(uint32_t), indirect);
    if (indirect == 0) {
      if (!force) co_return 0; // sparse file
      if (!(indirect = co_await provision_block(this, readonly.doubly_indirect_block, added_sectors))) goto finish;
      co_await device->write(readonly.doubly_indirect_block*block_size + number/ptrPerBlock*sizeof(uint32_t), indirect);
    }
    co_await device->read(indirect*block_size + (number%ptrPerBlock)*sizeof(uint32_t), offset);
    if (offset == 0 && force) {
      if (!(offset = co_await provision_block(this, readonly.doubly_indirect_block, added_sectors))) goto finish;
      co_await device->write(indirect*block_size + (number%ptrPerBlock)*sizeof(uint32_t), offset);
    }
  } else if ((number -= ptrPerBlockSq) < ptrPerBlockCb) // triply indirect
  {
    uint32_t indirect;
    if (readonly.triply_indirect_block == 0) {
      if (!force) co_return 0; // sparse file
      if (!(write.data().triply_indirect_block = co_await provision_block(this, readonly.doubly_indirect_block, added_sectors))) goto finish;
    }
    co_await device->read(readonly.triply_indirect_block*block_size + number/ptrPerBlockSq*sizeof(uint32_t), indirect);
    if (indirect == 0) {
      if (!force) co_return 0; // sparse file
      if (!(indirect = co_await provision_block(this, readonly.triply_indirect_block, added_sectors))) goto finish;
      co_await device->write(readonly.triply_indirect_block*block_size + number/ptrPerBlockSq*sizeof(uint32_t), indirect);
    }
    co_await device->read(indirect*block_size + (number%ptrPerBlockSq)/ptrPerBlock*sizeof(uint32_t), indirect);
    if (indirect == 0) {
      if (!force) co_return 0; // sparse file
      if (!(indirect = co_await provision_block(this, readonly.triply_indirect_block, added_sectors))) goto finish;
      co_await device->write(indirect*block_size + (number%ptrPerBlockSq)/ptrPerBlock*sizeof(uint32_t), indirect);
    }
    co_await device->read(indirect*block_size + (number%ptrPerBlock)*sizeof(uint32_t), offset);
    if (offset == 0 && force) {
      if (!(offset = co_await provision_block(this, readonly.triply_indirect_block, added_sectors))) goto finish;
      co_await device->write(indirect*block_size + (number%ptrPerBlock)*sizeof(uint32_t), offset);
    }
  }

finish:
  if (added_sectors > 0) {
    write.data().sectors += added_sectors * block_size / 512;
    co_await device->write(inode_offset, write.data());
  }

  co_return offset;
}

sync<uint32_t> Node::get_block(uint32_t number, bool force) {
  Mutex<Ext2Inode>::ROProof roProof{nullptr};
  Mutex<Ext2Inode>::Proof write{nullptr}; // nullptr means unlock() on move doesn't do anything

  if (!force) {
    roProof = std::move(co_await inode.readonly());
  } else {
    write = std::move(co_await inode);
  }

  const Ext2Inode& readonly = force ? write.data() : roProof.data();
  co_return co_await get_block(readonly, write, number, force);
}

sync<void> Node::truncate(uint32_t length) {
  auto proof = co_await inode;
  auto &data = proof.data();

  uint32_t size = data.size_low;
  if (length >= size) co_return;
  data.size_low = length;

  uint32_t start_free = (length+block_size-1)/block_size;
  uint32_t end_free = (size+block_size-1)/block_size + 1; // just make it excl

  if (start_free != 0) {
    uint32_t to_zero = (block_size - (length % block_size)) % block_size;
    if (to_zero != 0) {
      uint32_t block = co_await get_block(data, proof, start_free-1, false);
      if (block != 0) {
        auto *buffer = (char*) malloc(block_size);
        co_await device->read_all(block*block_size, block_size, buffer);
        memset(buffer + (block_size-to_zero), 0, to_zero);
        co_await device->write_all(block*block_size, block_size, buffer);
        free(buffer);
      }
    }
  }

  uint32_t count = 0;
  Ext2::BlockFree *to_free = nullptr;

  bool done = false;

  if (start_free < 12) { // direct
    for (uint32_t i = start_free; i < 12 && i < end_free; i++) {
      if (data.direct_block[i] == 0) continue;
      to_free = new Ext2::BlockFree{to_free, data.direct_block[i]};
      count++;
      data.direct_block[i] = 0;
    }
    start_free = 12;
    done |= start_free >= end_free;
  }

  start_free -= 12;
  end_free -= 12;

  if (!done && start_free < ptrPerBlock) { // singly indirect
    if (data.singly_indirect_block != 0) {
      auto *block = (uint32_t*)malloc(block_size);
      co_await device->read_all(data.singly_indirect_block*block_size, block_size, (char*)block);

      bool empty = true;
      for (uint32_t i = 0; empty && i < start_free; i++) empty &= block[i] == 0;

      for (uint32_t i = start_free; i < ptrPerBlock && i < end_free; i++) {
        if (block[i] == 0) continue;
        to_free = new Ext2::BlockFree{to_free, block[i]};
        count++;
        if (!empty) block[i] = 0;
      }

      if (empty) {
        to_free = new Ext2::BlockFree{to_free, data.singly_indirect_block};
        count++;
        data.singly_indirect_block = 0;
      } else {
        co_await device->write_all(data.singly_indirect_block*block_size, block_size, (char*)block);
      }

      free(block);
    }
    start_free = ptrPerBlock;
    done |= start_free >= end_free;
  }

  start_free -= ptrPerBlock;
  end_free -= ptrPerBlock;

  if (!done && start_free < ptrPerBlockSq) { // doubly indirect
    if (data.doubly_indirect_block != 0) {
      auto *first_block = (uint32_t*)malloc(block_size);
      auto *second_block = (uint32_t*)malloc(block_size);
      co_await device->read_all(data.doubly_indirect_block*block_size, block_size, (char*)first_block);

      uint32_t idx1 = start_free/ptrPerBlock;
      uint32_t idx2 = start_free%ptrPerBlock;

      bool first_empty = true;
      for (uint32_t i = 0; first_empty && i < idx1; i++) first_empty &= first_block[i] == 0;

      for (uint32_t i = idx1; i < ptrPerBlock && i <= end_free/ptrPerBlock; i++) {
        if (first_block[i] == 0) continue;
        uint32_t lbn = i * ptrPerBlock;
        co_await device->read_all(first_block[i]*block_size, block_size, (char*)second_block);

        bool second_empty = true;
        for (uint32_t j = 0; second_empty && i == idx1 && j < idx2; j++) second_empty &= second_block[j] == 0;
        first_empty &= second_empty;

        for (uint32_t j = i == idx1 ? idx2 : 0; j < ptrPerBlock && lbn+j < end_free; j++) {
          if (second_block[j] == 0) continue;
          to_free = new Ext2::BlockFree{to_free, second_block[j]};
          count++;
          if (!second_empty) second_block[j] = 0;
        }

        if (second_empty) {
          to_free = new Ext2::BlockFree{to_free, first_block[i]};
          count++;
          if (!first_empty) first_block[i] = 0;
        } else {
          co_await device->write_all(first_block[i]*block_size, block_size, (char*)second_block);
        }
      }

      if (first_empty) {
        to_free = new Ext2::BlockFree{to_free, data.doubly_indirect_block};
        count++;
        data.doubly_indirect_block = 0;
      } else {
        co_await device->write_all(data.doubly_indirect_block*block_size, block_size, (char*)first_block);
      }

      free(second_block);
      free(first_block);
    }

    start_free = ptrPerBlockSq;
    done |= start_free >= end_free;
  }

  start_free -= ptrPerBlockSq;
  end_free -= ptrPerBlockSq;

  if (!done && start_free < ptrPerBlockCb) { // triply indirect
    if (data.triply_indirect_block != 0) {
      auto *first_block = (uint32_t*)malloc(block_size);
      auto *second_block = (uint32_t*)malloc(block_size);
      auto *third_block = (uint32_t*)malloc(block_size);
      co_await device->read_all(data.triply_indirect_block*block_size, block_size, (char*)first_block);

      uint32_t idx1 = start_free/ptrPerBlockSq;
      uint32_t idx2 = (start_free%ptrPerBlockSq)/ptrPerBlock;
      uint32_t idx3 = start_free%ptrPerBlock;

      bool first_empty = true;
      for (uint32_t i = 0; first_empty && i < idx1; i++) first_empty &= first_block[i] == 0;

      for (uint32_t i = idx1; i < ptrPerBlock && i <= end_free/ptrPerBlockSq; i++) {
        if (first_block[i] == 0) continue;
        co_await device->read_all(first_block[i]*block_size, block_size, (char*)second_block);

        bool second_empty = true;
        for (uint32_t j = 0; second_empty && i == idx1 && j < idx2; j++) second_empty &= second_block[j] == 0;
        first_empty &= second_empty;

        uint32_t lbn = i*ptrPerBlockSq;
        for (uint32_t j = i == idx1 ? idx2 : 0; j < ptrPerBlock && lbn + j*ptrPerBlock < end_free; j++) {
          if (second_block[j] == 0) continue;
          co_await device->read_all(second_block[j]*block_size, block_size, (char*)third_block);

          bool third_empty = true;
          for (uint32_t k = 0; third_empty && i == idx1 && j == idx2 && k < idx3; k++) third_empty &= third_block[k] == 0;
          second_empty &= third_empty;
          first_empty &= second_empty;

          for (uint32_t k = i == idx1 && j == idx2 ? idx3 : 0; k < ptrPerBlock && lbn + j*ptrPerBlock + k < end_free; k++) {
            if (third_block[k] == 0) continue;
            to_free = new Ext2::BlockFree{to_free, third_block[k]};
            count++;
            if (!third_empty) third_block[k] = 0;
          }

          if (third_empty) {
            to_free = new Ext2::BlockFree{to_free, second_block[j]};
            count++;
            if (!second_empty) second_block[j] = 0;
          } else {
            co_await device->write_all(second_block[j]*block_size, block_size, (char*)third_block);
          }
        }

        if (second_empty) {
          to_free = new Ext2::BlockFree{to_free, first_block[i]};
          count++;
          if (!first_empty) first_block[i] = 0;
        } else {
          co_await device->write_all(first_block[i]*block_size, block_size, (char*)second_block);
        }
      }

      if (first_empty) {
        to_free = new Ext2::BlockFree{to_free, data.triply_indirect_block};
        count++;
        data.triply_indirect_block = 0;
      } else {
        co_await device->write_all(data.triply_indirect_block*block_size, block_size, (char*)first_block);
      }

      free(third_block);
      free(second_block);
      free(first_block);
    }
  }

  data.sectors -= count * block_size/512;

  co_await device->write(inode_offset, data);
  proof.unlock();
  if (to_free != nullptr) co_await ext2->free_blocks(to_free, count);
}
