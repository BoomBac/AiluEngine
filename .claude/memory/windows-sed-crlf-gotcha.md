---
name: windows-sed-crlf-gotcha
description: On Windows, sed -i destroys CRLF line endings; never run find+sed on all files
metadata:
  type: feedback
---

On this Windows project, `core.autocrlf=true` means working-tree files use CRLF and git stores LF. Running `find ... -exec sed -i` on ALL `.cpp`/`.h` files caused `sed -i` (MSYS2/Git Bash) to rewrite every file with LF endings, even when the sed pattern didn't match — producing 476 dirty files in `git status`.

**Why:** MSYS2 `sed -i` writes LF-terminated output. On `core.autocrlf=true` Windows repos, the working tree is expected to be CRLF. Touching every file with sed silently converts them all to LF, creating hundreds of phantom modifications.

**How to apply:**
1. NEVER use `find ... -exec sed -i` blindly on all files. First `grep -rl <pattern>` to find only files that need changes, then sed ONLY those files.
2. After `sed -i` on Windows, immediately run `unix2dos` on the changed files to restore CRLF.
3. Verify: `git diff --name-only | wc -l` and `git status --short | wc -l` should agree. If `git status` shows more files than `git diff`, there are line-ending issues.
4. Use `git checkout HEAD -- <file>` to restore files with only line-ending damage (no content changes).
