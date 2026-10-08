# AGENTS.md

Notes for AI agents working in this repository: **read this page first, then
`README.md`.** It covers how to build, run and verify the project and where the
sharp edges are — not what the app does.

---

## 1. What this repository is

This is a fork of the upstream **macdependency** project. Features that exist
only here are marked **`*This fork.*`** in `README.md` — **keep that marking up
to date** when you touch a feature, otherwise a reader cannot tell what is
upstream and what is ours.

Work on the fork. Do not push to the upstream repository.

---

## 2. Layout

```
MachO/                          # the parsing library (C++, a framework target)
├── MachO.xcodeproj
├── MachO_Prefix.h              # library prefix header
├── machoarchitecture.*         # one architecture: segments/sections, read by address
├── machoheader.*               # MachOHeader::FileType (MH_EXECUTE / MH_DYLIB / …)
├── dylibcommand.*              # DylibCommand::DependencyType (Normal / Weak / Delayed / Id)
├── internalfile.*              # InternalFile / DiskInternalFile / MemoryInternalFile
├── memoryinternalfile.*        # an image that only exists in the dyld shared cache
├── objcmetadata.*              # Objective-C metadata parsing
├── objctypedecoder.*           # Objective-C type-encoding decoding
├── symboltableentry*.{h,cpp}   # SymbolTableEntry::Type / Kind
└── dyldcacheimage.*            # images served from the shared cache

MacDependency/                  # the Cocoa document app (Objective-C++, ARC)
├── MacDependency.xcodeproj
├── MacDependency_Prefix.h
├── MyDocument.{h,mm}           # the document window; wires table, tree and log together
├── MachOModel.{h,mm}           # one row of the dependency tree (fileKind / textColor)
├── SymbolTableController.{h,mm}
├── SymbolTableHeaderView.{h,m} # header chevrons + filter menus (this fork)
├── SymbolTableEntryModel.*
├── SymbolTableEntryTypeFormatter.*   # Export / Import labels
├── SymbolTableEntryKindFormatter.*   # C / C++ / Swift / ObjC class / … labels
├── ObjCClassDefinitionWindowController.*
├── Base.lproj/*.xib            # MyDocument.xib / MainMenu.xib
└── en.lproj/Localizable.strings      # UTF-16LE!

docs/
└── objc-class-definition-progress.md   # the main progress log for the ObjC work
.github/workflows/build.yml     # CI: builds MacDependency on macos-latest
images/
```

`MachO.xcodeproj` is a **subproject** of `MacDependency.xcodeproj`, and the app
links the library as `MachO.framework`. There are exactly two schemes:
`MacDependency` and `MachO`. There is no test target — CI only builds the app.

---

## 3. Building

Run this from the repository root:

```bash
xcodebuild -project MacDependency/MacDependency.xcodeproj \
           -scheme MacDependency \
           -configuration Debug \
           -derivedDataPath /tmp/macdep-derived \
           MACOSX_DEPLOYMENT_TARGET=12.0 \
           build
```

The product lands in
`/tmp/macdep-derived/Build/Products/Debug/MacDependency.app`.

**Sharp edges:**

- **You must override the deployment target.** Both projects set
  `MACOSX_DEPLOYMENT_TARGET` at *target* level (a recent macOS version) and at
  *project* level (a very old one). The SDK on a current machine will not accept
  a deployment target below 12.0, so always pass
  `MACOSX_DEPLOYMENT_TARGET=12.0` on the command line.
- **`-derivedDataPath` requires `-scheme`.** With only `-target` it fails with
  `The flag -scheme, -testprojectPath, or -xctestrun is required`.
- Keep derived data out of the working tree; `/tmp` is easiest. Any dot-directory
  at the top level is ignored by `.gitignore` (`/.*`), so build output and local
  agent state never get committed.
- Two warnings are pre-existing and not caused by your change: a headermap
  warning from the `MachO` project, and a deprecated
  `preparedCellAtColumn:row:` in `MyDocument.mm`.
- If you only changed library code under `MachO/` and just want to know whether
  it compiles, building the `MachO` scheme alone is much faster.

---

## 4. Code conventions

### Library side, `MachO/` (plain C++)

- C++11 / libc++, public types marked with the `EXPORT` macro.
- Forward-declare in headers when you can (e.g. `machoarchitecture.h` only needs
  `class SegmentCommand;`).
- Reading a **memory-mapped image** always goes through
  `MachOArchitecture::readFromProcess()` (which uses `vm_read_overwrite`).
  **Never dereference the address directly** — it is not guaranteed readable.
- A new `.cpp` needs `settings = {COMPILER_FLAGS = "-fno-objc-arc"; }` in
  `MachO.xcodeproj`, matching the rest of the library.

### App side, `MacDependency/` (Objective-C++, ARC)

- New files are `.mm` (Objective-C++) or `.m` (plain Objective-C); both are ARC.
- A `@property` declares an ivar named `_xxx`; writing `xxx` inside a method body
  will not compile — write `self.xxx`.
- **Do not let two objects strongly reference each other under ARC.** The header
  view registers itself with the controller in `awakeFromNib`, and the controller
  keeps its headers in a `[NSHashTable weakObjectsHashTable]`.

### Why the two halves are separate targets

- Both projects enable `GCC_PRECOMPILE_PREFIX_HEADER`. `MachO_Prefix.h` and
  `MacDependency_Prefix.h` pull in Cocoa plus the C++ standard headers, and the
  sources rely on them without including anything themselves.
- The library is plain C++ and the app is ARC Objective-C++; their compiler flags
  are incompatible, so they cannot be compiled as one target.
- App sources use directory-qualified includes such as `#import "MachO/macho.h"`,
  so the repository root must be on the include path. That is what
  `HEADER_SEARCH_PATHS = ./../` in the project file means (one level up from
  `MacDependency/`).

### Compile traps seen in this codebase

| Written as | Error | Write instead |
|---|---|---|
| `rectA == rectB` (`NSRect`) | `invalid operands to binary expression` | `NSEqualRects(rectA, rectB)` |
| `filterController` (bare ivar name) | `use of undeclared identifier` | `self.filterController` |
| `[self numberOfColumns]` | `no visible @interface ... declares the selector` | `numberOfColumns` belongs to `NSTableView`, not `NSTableHeaderView` |

---

## 5. Hand-edited resource files

These are maintained by hand or by script; be careful with them.

### `Base.lproj/MyDocument.xib`

- Validate with
  `ibtool --compile /tmp/x.nib MacDependency/Base.lproj/MyDocument.xib` — if that
  fails, the xib is malformed.
- Header filtering identifies columns by **column identifier**: `Type`, `Kind`,
  `Name`. Changing an identifier breaks
  `SymbolTableController -hasFilterMenuForTableColumn:`.
- `columnAutoresizingStyle` on the outline view:
  - `lastColumnOnly` — the last column takes the slack;
  - `firstColumnOnly` — the first column takes the slack. The dependency tree
    uses this so `Name (Install Name)` absorbs the width.

### `en.lproj/Localizable.strings`

**This file is UTF-16LE.** Appending with `echo >>` or `cat >>` corrupts the
encoding and turns every string in the app into a lookup key.

```python
# Read and write it as utf-16 with Python, then append the new keys.
import io
path = "MacDependency/en.lproj/Localizable.strings"
with io.open(path, "r", encoding="utf-16") as f:
    text = f.read()
# ...edit text...
with io.open(path, "w", encoding="utf-16") as f:
    f.write(text)
```

### `*.xcodeproj/project.pbxproj`

A new source file has to be registered by hand in **four** places, using
24-digit hexadecimal IDs of our own making:

- `PBXBuildFile` section:
  `B3C0DE0F1A2B3C4D5E0001xx /* Foo.m in Sources */ = {... fileRef = ...0000xx ...};`
- `PBXFileReference` section:
  `B3C0DE0F1A2B3C4D5E0000xx /* Foo.m */ = {...};`
- the owning `PBXGroup`'s `children`, and the target's `PBXSourcesBuildPhase`
  `files`.

(`MachO` uses the `B2C0DE0F...` prefix, `MacDependency` uses `B3C0DE0F...`; do not
reuse a number within one project.)

---

## 6. Documentation and notes

- **`docs/objc-class-definition-progress.md`** is the main progress log for the
  Objective-C class-definition work line. **Update it when you reach a milestone,
  before moving on.** A feature belonging to that line goes in there; a new work
  line gets its own `docs/*.md`.
- **`README.md`** is user-facing. Keep it in step with the code:
  - mark fork-only features with `*This fork.*`;
  - the tables (the dependency tree, the symbol table sections) must agree with
    the code on column names and values.
- Development-time verification harnesses are **not** part of the repository.
  How to rebuild and run them is recorded in the project notes alongside the
  working copy, not here.

---

## 7. Runtime traps

- **The dependency graph has cycles** (Foundation depends on something that
  depends on Foundation). Walking `MachOModel.children` **must deduplicate by
  filename**, or memory blows up and the process is OOM-killed (exit 137).
- **A symbol's kind can only be told from its name prefix** — there is no
  metadata for it. The protocol prefix is `__OBJC_PROTOCOL_$_` (note the **two**
  leading underscores).
- **Protocol definition symbols are always `local`**, so with the Type filter set
  to Export/Import you will not see `ObjC protocol` rows at all. That is not a
  bug.
- **Type and Kind are two independent filters** sharing one
  `SymbolTableController`. The header menu and the on-table `Export` / `Import`
  buttons are **two views of the same state** and must write back to each other:
  - `typeFilterMask` is the single source of truth (bit 0 = Export, bit 1 = Import);
  - the header menu is **single-select** (`All types` / `Export` / `Import`), the
    button group is a two-segment `selectAny`;
  - **the last remaining type must not be switched off** (that would leave an
    empty table); `updateTypeFilterControl` rolls it back.
- **Measure column widths against the longest label** with `sizeWithAttributes:`
  instead of guessing. For reference: `Shared Cache` 85pt, `Executable File`
  91pt, `ObjC metaclass` 95pt, `Dynamic Library` 98pt, `System Shared Cache`
  133pt, `Link Type` 58pt.

---

## 8. Suggested workflow

1. Decide which side the change belongs to: `MachO/` (parsing) or
   `MacDependency/` (UI, bindings). The two halves compile with different flags,
   so mixing them up produces confusing link errors.
2. Run `xcodebuild` to confirm it compiles (the `MachO` scheme is a fast check for
   library-only changes).
3. If you touched a xib, an outlet or a binding, syntax-check the xib with
   `ibtool` and make sure the app still opens a document without raising.
4. Update the progress doc under `docs/` and `README.md` (see §6).
5. Commit messages are English and imperative, matching the existing history
   (e.g. `Fix indent`, `Mark libraries served from the dyld shared cache in a
   distinct color`).

---

## 9. Command cheat sheet

```bash
# Build the app (from the repository root)
xcodebuild -project MacDependency/MacDependency.xcodeproj \
  -scheme MacDependency -configuration Debug -derivedDataPath /tmp/macdep-derived \
  MACOSX_DEPLOYMENT_TARGET=12.0 build

# Run it
open /tmp/macdep-derived/Build/Products/Debug/MacDependency.app

# Syntax-check a xib
ibtool --compile /tmp/MyDocument.nib MacDependency/Base.lproj/MyDocument.xib

# List the schemes
xcodebuild -list -project MacDependency/MacDependency.xcodeproj
```

> **Note: the default shell here is fish.** For loops, conditionals or multi-line
> scripts, write a `.sh` file and run it with `bash /tmp/x.sh`, or wrap the
> command in `bash -c '...'`. Heredocs, `$?` and `for ... done` typed straight
> into fish will fail. For searching, use `rg` — the BSD `grep` shipped here does
> not support `\|` alternation and **fails silently**, returning nothing.
