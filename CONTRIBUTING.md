Repository formatting, style, and contribution guidelines
=====================================================

This file documents the project's formatting and style requirements and provides quick commands
for applying the formatter and basic static checks. Follow these guidelines to keep the codebase
consistent and review-ready.

1. Formatting (clang-format)
----------------------------

- The repository contains a `.clang-format` at the project root. Use `clang-format` to apply
  formatting in-place.

  Run (from project root):

  ```bash
  # verify clang-format is installed
  clang-format --version

  # format all C sources and headers in-place
  find . -type f \( -name '*.c' -o -name '*.h' \) -not -path './.git/*' -print0 \
    | xargs -0 clang-format -i
  ```

- Key formatting decisions (mirrored in `.clang-format`):
  - BasedOnStyle: Google
  - IndentWidth: 4 (spaces, never tabs)
  - PointerAlignment: Left
  - ColumnLimit: 100
  - BreakBeforeBraces: Attach

- After formatting run `git diff` to review changes before committing.

2. Static Analysis (clang-tidy)
-------------------------------

- We include a `.clang-tidy` config. Run `clang-tidy` as part of local checks if available. A basic invocation:

  ```bash
  # assuming a build directory with compile_commands.json
  clang-tidy path/to/file.c -- -Iinclude -Itests/vendor
  ```

- You may need to generate `compile_commands.json` for full clang-tidy capabilities.

3. Documentation and Comments
-----------------------------

- Use Doxygen-style comments for public APIs and modules. Keep descriptions concise and
  document parameters, return values, and any side-effects.

Commenting style
--------------------------------

- File header: each C source should start with a block comment describing the file name (@file),
  purpose, author/license (@author), and the full license text. Use a C-style block with two
  leading asterisks so it is easily picked up by documentation tools, for example:

  /**
   * @file filename.c
   *   ...
   */

- File header: each C header files should follow the same format as C files.

- Function-level comments: use Doxygen-like block comments immediately above functions.
  Start with a one-line summary and follow with a concise paragraph if more detail is
  required. Document if parameters are expected, document parameters using the `@param` 
  tag. If a return value is supplied, document with the `@return` tag.
  Example:

  /**
   * @brief This function does something.
   *
   * This is a more detailed description of how the function works from 
   * a user perspective.
   *
   * @param param1 - This paramter is ....
   * @return uint8_t Returns 1 if the valid, 0 otherwise.
   */

- Function-level comments: do not provide funciton-level comments for function declarations
  whether in C files or header files.  If multiple declarations exist that are logically grouped
  together (such as forward references) it is acceptable to have a single line commnet that 
  explains the purpose of the declarations (but not full function-level commenting).
  
- Inline comments: use `/* ... */` or `//` for short explanations within functions. Prefer
  brief sentences that explain the "why" rather than the "what" (the code shows the
  what). Keep them short and place them on their own line when explaining a block of code.

- Tag usage: prefer these tags when applicable: `@file`, `@brief`, `@param`, `@return`,
  and `@author`. Keep tags lowercase as in the examples above.

- Empty lines: separate the block comment from the function signature with a single blank line.
 - Empty lines: do not place a blank line between a function's Doxygen block
   comment and its function definition; the block comment should be immediately
   above the function signature.

- Don't repeat obvious information: avoid comments that restate the code; instead document
  intent, invariants, assumptions, side-effects, and reasons for non-obvious decisions.

- Licensing and attribution: include copyright/license information in the file header when
  appropriate.

4. C Style Rules (project preferences)
-------------------------------------

- Avoid `(void)` in parameter lists; prefer an empty parameter list for C functions when
  appropriate.
- Use explicit types (`uint8_t`, `uint16_t`, etc.) from `<stdint.h>` in public interfaces.
- Keep functions small and focused; prefer clear naming over clever abbreviations.
- Keep internal symbols `static` unless they are part of the public API.

5. Tests
--------

- Unit tests are under `tests/` and are built with `-DUNIT_TEST` for host execution. Tests
  should not add production-only helpers directly into source files — use the provided
  `mctp_testhooks.h` mechanism for test-only access.

6. Pre-commit / CI suggestions
-----------------------------

- We recommend adding a lightweight pre-commit hook to run `clang-format` on staged C files.
  Example `./.githooks/pre-commit` snippet:

  ```bash
  #!/usr/bin/env bash
  files=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(c|h)$' || true)
  if [ -n "$files" ]; then
    echo "Running clang-format on staged C files..."
    echo "$files" | xargs -r clang-format -i
    # re-add formatted files to the index
    echo "$files" | xargs -r git add
  fi
  ```

- Make the hook executable: `chmod +x .githooks/pre-commit` and enable with `git config core.hooksPath .githooks`.

7. Commit messages and PRs
-------------------------

- Keep commit messages short and descriptive. For larger changes, include a brief summary
  and rationale in the commit body.

8. Questions or exceptions
--------------------------

- If you need to deviate from these rules for a specific, justified reason, add a comment
  to the PR explaining why and request reviewer approval.

Thanks for contributing — consistent style keeps the code review process fast and focused.
