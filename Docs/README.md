# SCP:CB Wii U documentation

Developer documentation and project memory. Start here when picking the project back up, whether you're a
person or an AI assistant in a new chat.

| File | What's in it |
|---|---|
| [PROJECT.md](PROJECT.md) | What the project is, its history, the decisions made so far and why, and the owner's working preferences |
| [FEASIBILITY.md](FEASIBILITY.md) | The feasibility study (2026-10-13) : what exists, what has to be written, risks, effort, verdict |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Design : Wii U back end (GX2 renderer, canvases, files, endianness), ported Blitz3D runtime, the bb2cpp transpiler |
| [RUNTIME_API.md](RUNTIME_API.md) | The exact Blitz3D commands / intrinsics / DLLs the 1.3.11 exe uses, by runtime module (generated) |
| [BUILDING.md](BUILDING.md) | Requirements, build steps, the PC tools (bb2cpp, texconv...), reference material in `_ref/`, testing on hardware and in Cemu (automated runs, scripted input) |
| [STATUS.md](STATUS.md) | Current state, test results, hardware test checklist, controls, known issues, roadmap |
| [tools/](tools/) | Helper scripts : `list_builtins.sh` (regenerates the builtin list for RUNTIME_API.md from a disassembly) |

Keep these files up to date when something important changes. In particular, record decisions (and why) in
PROJECT.md and test results in STATUS.md.
