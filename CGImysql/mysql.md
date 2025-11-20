list<MYSQL *> connList:
这里为什么不用 unique_ptr？
因为 MYSQL 对象通常是由 MySQL C 库 (mysql_init) 分配的，而且我们需要频繁地在池子和用户之间传递这个指针。
虽然可以用智能指针配自定义删除器，但对于连接池这种**“借出-归还”模型，
且生命周期由池子统一管理**的情况，用原始指针配合 DestroyPool 统一释放，性能更好且代码更直观。

sem reserve 的作用:
这是一个非常经典的信号量用法。
初始值 = MaxConn（比如 10）。
GetConnection (P操作): reserve.wait()。如果池子空了，线程就会在这里阻塞（睡觉），直到有人归还连接。这避免了轮询，非常高效。
ReleaseConnection (V操作): reserve.post()。归还连接后，信号量+1，唤醒一个正在排队等连接的线程。