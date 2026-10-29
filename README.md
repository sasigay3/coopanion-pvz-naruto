# Coopanion PvZ Naruto

An independent Coopanion extension for autonomous Plants vs. Zombies Naruto Edition gameplay.

**Status: research and adaptation only. This project is not yet usable with the game.**

This project will derive from [coopanion-pvz-garden](https://github.com/sasigay3/coopanion-pvz-garden) and [cortico-world-pvz](https://github.com/Phantivia/cortico-world-pvz). It does not distribute the game, its assets, or save data.

A minimal World definition has an isolated `pvz-naruto` identity and intentionally refuses activation or creation until the game-specific bridge has been verified. It exposes no tools. See [compatibility research](docs/compatibility.md) for the evidence and outstanding work. Do not install this development scaffold as a working extension.

Development checks: `corepack pnpm typecheck`, `corepack pnpm test`, and `python -m unittest discover -s tests`.
