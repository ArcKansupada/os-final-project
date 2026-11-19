/* Copyright (C) 2026 Ahmed Gheith and contributors.
 *
 * Use restricted to classroom projects.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include "filesystem/ext2.h"
#include "lib/shared.h"
#include "async/sync.h"
#include <cstdint>

/*
 * A minimal ELF loader: 64-bit, little-endian, x86-64, ET_EXEC (statically
 * linked, not relocatable). Only PT_LOAD segments matter, everything else is
 * ignored: no interpreter, no dynamic linking, no TLS setup.
 */
namespace ELF {

extern sync<uint64_t> load(StrongRef<Node> file);

} // namespace ELF
