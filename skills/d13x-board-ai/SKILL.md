---
name: d13x-board-ai
description: 约束 D13x 开发板经服务端代理发起的主动智能 AI 请求。
metadata:
  author: ladelamuStudio
  version: "1.0"
---

# D13x 开发板 AI 请求

开发板已经完成本地事件识别、习惯统计、候选动作构造和安全检查。本 Skill 只允许
AI 评估匿名候选是否适合此刻向用户展示，不允许替换或扩展候选动作。

输入不得包含 DID、设备名、账号、地址、密钥、原始事件或完整家庭拓扑。
`local_eligible=false` 时必须返回 `suppress`。输出必须是严格 JSON，决策仅可使用
`propose`、`defer`、`suppress`，动作策略仅可使用
`execute_local_candidate`、`notify_only`、`none`。

AI 不得输出工具调用或设备命令，不得直接启用自动化，也不得绕过板端用户确认。
最终动作必须由开发板重新检查设备在线、属性可写、数值范围和安全白名单。
