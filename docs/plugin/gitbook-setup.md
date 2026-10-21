# GitBook Setup

This directory is structured for GitBook Git Sync.

## Recommended sync root

Point the GitBook space at:

```text
docs/plugin
```

The root `README.md` is the documentation landing page and `SUMMARY.md` defines the page order/navigation.

## Repository workflow

Use GitBook's Git Sync with the repository/branch containing IW8x and select this documentation subdirectory as the synced content root.

Once sync is enabled, documentation edits can be made either in Markdown in the repository or through GitBook's editor.

## Source of truth

The public API source lives in:

```text
src/plugin/sdk/abi.h
src/plugin/sdk/gsc.hpp
src/plugin/sdk/value.hpp
```

The implementation used to verify behavior lives primarily in:

```text
src/plugin/
src/gsc/
src/bootstrap/native_plugins.cpp
```

When ABI behavior changes, update these GitBook pages in the same change as the code.

