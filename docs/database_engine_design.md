# 数据库引擎设计说明

负责人：wzy

开发过程中补充以下内容：

- 持久化 Catalog 格式；
- Row 序列化与反序列化；
- 页内记录与删除标记布局；
- CreateTable、Insert、SeqScan、Filter、Project、Delete 算子；
- CLI 输入输出与错误展示；
- 跨页查询和重启恢复；
- 端到端测试结果。
