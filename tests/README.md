# 测试目录

本目录保存可重复执行的验收步骤，不提交包含账号凭据的原始日志。总体验收状态见
[`VALIDATION.md`](../VALIDATION.md)。

| 目录 | 范围 |
| --- | --- |
| `boot/` | tinySPL、NuttX、NSH 冷启动与复位 |
| `uart/` | UART0 收发、长日志与交互稳定性 |
| `i2c/` | I2C2 节点及 GT911 间接链路 |
| `display/` | LVDS、framebuffer、背光和刷新 |
| `touch/` | GT911 原始样本、全屏触点和 UI 交互 |
| `timer/` | CORET、调度、系统时间和 NTP |
| `ethernet/` | PHY、DHCP、DNS、网关和断线恢复 |
| `persistence/` | 米家登录信息的重启与升级保持 |
| `proactive/` | 主动智能冷启动、通用行为触发、回放隔离、反馈、设备执行和画像恢复 |
| `stress/` | 长稳、反复切页、网络和内存观察 |
| `xts/` | XTS 套件清单、执行结果与适用性说明 |

每轮测试先复制 [`RESULT_TEMPLATE.md`](RESULT_TEMPLATE.md) 作为结果文件。原始证据
应放在 Release 附件、PR 附件或外部只读存储，并在结果文件中记录链接和 SHA-256。

也可以用脚本创建一次本地测试记录，并自动保存镜像哈希和源码状态：

```bash
./tests/new-run.sh /path/to/d13x_hengshan-pi_v1.12.15.8.img
```

结果默认写入被 Git 忽略的 `validation-artifacts/<version>/<run-id>/`。完成并
脱敏后，再选择
需要提交的汇总或上传为 PR/Release 附件，避免误传账号凭据和大型原始日志。
