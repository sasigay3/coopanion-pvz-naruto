# Coopanion PvZ Naruto

这是一个面向火影版《植物大战僵尸》的独立 Coopanion 扩展项目，基于 [高清版扩展](https://github.com/sasigay3/coopanion-pvz-garden) 和 [上游项目](https://github.com/Phantivia/cortico-world-pvz) 的设计开展适配。

**当前状态：开发与兼容性研究中，尚不能作为游戏扩展使用。** World 使用独立的 `pvz-naruto` 标识，在适配完成前会拒绝启动，尚未提供游戏控制工具。不要将当前开发骨架当作可玩的版本安装。

项目不包含游戏程序、游戏素材、存档或私人配置。目标游戏需要使用者自行准备。当前已观察的具体文件和验证范围见 [兼容性记录](docs/compatibility.md)；文件指纹匹配并不代表已完整支持。

当前工作包括只读的程序结构分析、受限进程检查，以及独立的原生数组读取、候选快照和只读进程校验组件。原生进程校验尚未对运行中的目标游戏做正向实测，也没有接入扩展；棋盘观察、动作、自动游玩、特殊关卡和辅助功能均未完成。

开发检查：

```powershell
corepack pnpm typecheck
corepack pnpm test
python -m unittest discover -s tests
scripts/test-native.cmd
```

最后一项需要 Windows 和 Visual Studio x86 C++ Build Tools。原生测试不会启动游戏。

遵循上游 MIT 许可证；详见 [LICENSE](LICENSE)。
