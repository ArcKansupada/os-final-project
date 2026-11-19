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

#include "user_context.h"

#include "lib/debug.h"
#include <cstddef>

UserContext::UserContext()
    : rax(0), rbx(0), rcx(0), rdx(0), rsi(0), rdi(0), rbp(0), r8(0), r9(0),
      r10(0), r11(0), r12(0), r13(0), r14(0), r15(0), rip(0), rsp(0), rflags(0),
      fs_base(0), gs_base(0), fpu{} {}

static_assert(offsetof(UserContext, rax) == 0);
static_assert(offsetof(UserContext, rbx) == 8);
static_assert(offsetof(UserContext, rcx) == 16);
static_assert(offsetof(UserContext, rdx) == 24);
static_assert(offsetof(UserContext, rsi) == 32);
static_assert(offsetof(UserContext, rdi) == 40);
static_assert(offsetof(UserContext, rbp) == 48);
static_assert(offsetof(UserContext, r8) == 56);
static_assert(offsetof(UserContext, r9) == 64);
static_assert(offsetof(UserContext, r10) == 72);
static_assert(offsetof(UserContext, r11) == 80);
static_assert(offsetof(UserContext, r12) == 88);
static_assert(offsetof(UserContext, r13) == 96);
static_assert(offsetof(UserContext, r14) == 104);
static_assert(offsetof(UserContext, r15) == 112);
static_assert(offsetof(UserContext, rip) == 120);
static_assert(offsetof(UserContext, rsp) == 128);
static_assert(offsetof(UserContext, rflags) == 136);
static_assert(offsetof(UserContext, fs_base) == 144);
static_assert(offsetof(UserContext, gs_base) == 152);
static_assert(offsetof(UserContext, fpu) == 160);
