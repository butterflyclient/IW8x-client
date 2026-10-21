# Status Codes

Raw ABI functions return `iw8x_status`.

| Constant | Meaning |
| --- | --- |
| `IW8X_OK` | Operation succeeded |
| `IW8X_INVALID_ARGUMENT` | Invalid pointer, shape, name, scope, or argument |
| `IW8X_UNSUPPORTED` | API exists but runtime capability is unavailable |
| `IW8X_DUPLICATE` | Name/registration already exists |
| `IW8X_NOT_FOUND` | Requested registration/task/plugin does not exist |
| `IW8X_WRONG_THREAD` | Operation requires an active server/GSC engine boundary |
| `IW8X_STALE` | Invocation/entity/handle/scope is no longer valid |
| `IW8X_STOPPING` | Plugin is unloading or no longer accepting callbacks |
| `IW8X_BUSY` | Resource/task/plugin cannot be changed while active |
| `IW8X_TYPE_MISMATCH` | Value type did not match requested C++/ABI type |
| `IW8X_CALLBACK_ERROR` | Plugin callback reported or threw an error |
| `IW8X_LIMIT` | Registry, nesting, size, or identifier limit reached |
| `IW8X_VERSION_MISMATCH` | Plugin ABI version/host structure mismatch |
| `IW8X_INTERNAL_ERROR` | Host-side failure that did not fit another status |

The C++ SDK's `iw8x::check()` converts nonzero status values into `iw8x::api_error`.

