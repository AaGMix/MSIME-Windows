# 公共辅助码

供 Windows、Apple 和 Linux 共用的辅助码数据。

所有辅助码文件统一放在 `helpcodes\` 目录下。当前 server 使用的方案和文件为：

- `lantian`：`helpcode.txt`（蓝天小雨点）
- `ziranma`：`zrm_helpcode_big_unique.txt`（自然码）
- `shouyou2_0`：`shouyou2_0_helpcode.txt`（首右2.0）
- `shouyouplus`：`shouyouplus_helpcode.txt`（首右plus）
- `xiaohe`：`xiaohe_helpcode.txt`（小鹤）
- `jiajia`：`jiajia_helpcode.txt`（加加）

公共数据位于 Engine 的 `helpcode/helpcodes/`，平台打包时复制需要的文件到应用资源目录。运行时路径由平台传给引擎，不依赖作者机器的绝对路径。合仓后的消费端无需再单独检出 HelpCode 仓库。

## 取码规则

各方案的取码规则不同，写文档时不要互相套用：

- `jiajia`（加加）：按笔顺拆出前两个部件，各取读音首字母（妈 = 女 n + 马 m = `nm`，暗 = 日 r + 音 y = `ry`）。
  部件按拼音加加的俗称读音取音：亻=d（单人旁）、扌=t（提手旁）、纟=j（绞丝旁）、忄=s（竖心旁）、
  攵=f（反文旁）、夂=z（折文儿）、刂=l（立刀旁）、彳=s（双人旁）。
- `jiajia` 的独体字与部首字取「本字声母 + 起笔笔画」（h 横、s 竖、p 撇、n 点/捺、z 折），共 138 个字。
  这是与拼音加加原表刻意保留的差别：加加对这些字给的是另一套拆法（笔画码或不同的部件拆分）。来源与授权见 [NOTICE.md](./NOTICE.md)。

## 参考

- 自然码辅助码：<https://github.com/copperay/ZRM_Aux-code>
