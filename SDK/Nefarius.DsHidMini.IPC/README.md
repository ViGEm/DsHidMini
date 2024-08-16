﻿# <img src="../../assets/FireShock.png" align="left" />Nefarius.DsHidMini.IPC

![Requirements](https://img.shields.io/badge/Requires-.NET%208.0-blue.svg)
[![NuGet Version](https://img.shields.io/nuget/v/Nefarius.DsHidMini.IPC)](https://www.nuget.org/packages/Nefarius.DsHidMini.IPC/)
[![NuGet](https://img.shields.io/nuget/dt/Nefarius.DsHidMini.IPC)](https://www.nuget.org/packages/Nefarius.DsHidMini.IPC/)

Interop SDK for DsHidMini IPC mechanism.

## Documentation

[Link to API docs](docs/index.md).

### Generating documentation

- `dotnet build -c:Release`
- `dotnet tool install -g Nefarius.Tools.XMLDoc2Markdown`
- `xmldoc2md .\bin\Release\net8.0-windows\Nefarius.DsHidMini.IPC.dll .\docs\`
