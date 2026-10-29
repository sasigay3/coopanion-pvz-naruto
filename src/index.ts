import type { WorldDefinition } from 'cortico/world.ts';

export interface NarutoConfig {
  enabled: boolean;
  executable: string;
}

/** Explicitly fail closed until target-specific observation and actions are verified. */
function unsupported(): never {
  throw new Error('火影版适配尚未完成：当前版本不能启动游戏或连接进程。');
}

export const PVZ_NARUTO: WorldDefinition<NarutoConfig> = {
  id: 'pvz-naruto',
  label: '植物大战僵尸火影版（开发中）',
  defaults: () => ({ enabled: false, executable: '' }),
  preflight: unsupported,
  create: unsupported,
  describe: { tools: [], events: [] },
};

export default PVZ_NARUTO;
