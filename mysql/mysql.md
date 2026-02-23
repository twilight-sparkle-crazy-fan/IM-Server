# MySQL 连接池 — 业务逻辑流程

## 一、整体架构

```
┌──────────────┐     getConn()      ┌──────────────────┐    mysql_query()   ┌─────────┐
│ Worker Thread │ ──────────────── → │   SqlConnPool    │ ← ─ ─ ─ ─ ─ ─ →  │  MySQL  │
│ (ChatSession) │ ← ─ shared_ptr ── │  (connQueue_)    │    长连接复用       │  Server │
└──────────────┘   析构时自动归还    └──────────────────┘                    └─────────┘
```

> 核心思想：**预创建 N 条长连接放入队列，Worker 线程按需借用，用完自动归还，全程无需手动管理连接生命周期。**

---

## 二、完整业务流程图

```mermaid
flowchart TD
    subgraph INIT["🔧 初始化阶段 · main → SqlConnPool::init"]
        A["main() 启动"] --> B["SqlConnPool::init\n(host, port, user, pwd, db, size)"]
        B --> C{"循环 i < connSize"}
        C -->|"每次迭代"| D["mysql_init() 创建句柄"]
        D --> E["mysql_options()\n超时 / 自动重连 / utf8mb4"]
        E --> F["mysql_real_connect()\n建立 TCP 长连接"]
        F --> G{"连接成功?"}
        G -->|"Yes"| H["connQueue_.push(conn)"]
        G -->|"No"| I["stderr 记录错误, continue"]
        H --> C
        I --> C
        C -->|"循环结束"| J["closed_ = false\nmaxConn_ = queue.size()"]
    end

    subgraph GET["📤 获取连接 · Worker 调用 getConn()"]
        K["Worker 线程\n调用 getConn()"] --> L{"closed_ ?"}
        L -->|"Yes"| M["return nullptr"]
        L -->|"No"| N["unique_lock 加锁"]
        N --> O["cond_.wait()\n阻塞等待: 队列非空 ∨ closed"]
        O --> P{"队列空 ∨ closed ?"}
        P -->|"Yes"| M
        P -->|"No"| Q["conn = queue.front()\nqueue.pop()"]
        Q --> R["mysql_ping() 检活\n断线则自动重连"]
        R --> S["返回 shared_ptr·MYSQL\n绑定自定义删除器 freeConn"]
    end

    subgraph USE["⚙️ 业务使用 · ChatSession 持有连接"]
        S --> T{"业务类型?"}
        T -->|"LOGIN"| U["SELECT User\n验证用户名 + 密码"]
        T -->|"REGISTER"| V["INSERT User\n创建新用户"]
        T -->|"CHAT · 目标离线"| W["INSERT OfflineMessage\n存储离线消息"]
        T -->|"LOGIN 成功后"| X["SELECT OfflineMessage\n拉取 → 发送 → DELETE"]
        T -->|"ADD_FRIEND"| Y["INSERT Friend × 2\n双向插入好友关系"]
        T -->|"GET_FRIENDS"| Z["SELECT Friend JOIN User\n返回好友列表 + 在线状态"]
    end

    subgraph RET["♻️ 归还连接 · shared_ptr 析构"]
        U & V & W & X & Y & Z --> AA["shared_ptr 析构\n→ freeConn() 被调用"]
        AA --> AB{"closed_ ?"}
        AB -->|"Yes"| AC["mysql_close()\n直接释放"]
        AB -->|"No"| AD["lock → connQueue_.push(conn)"]
        AD --> AE["cond_.notify_one()\n唤醒等待中的 Worker"]
    end

    subgraph CLOSE["🛑 关闭阶段 · 信号处理 / 析构"]
        AF["closePool()"] --> AG["closed_.exchange(true)\n幂等保护"]
        AG --> AH["cond_.notify_all()\n唤醒所有阻塞的 getConn"]
        AH --> AI["循环 pop → mysql_close()\n逐个释放所有连接"]
    end

    J -.->|"就绪"| K
    AE -.->|"连接回池"| O
```

---

## 三、关键设计要点

### 1. RAII — 零泄漏连接管理

```cpp
// getConn() 返回的 shared_ptr 绑定了自定义删除器
return std::shared_ptr<MYSQL>(conn, [this](MYSQL* c) {
    this->freeConn(c);  // 析构时自动归还，而非 mysql_close
});
```

调用方只需：
```cpp
auto conn = SqlConnPool::getInstance().getConn();
mysql_query(conn.get(), "SELECT ...");
// 离开作用域 → shared_ptr 析构 → freeConn → 连接回池
```

### 2. 线程安全 — mutex + condition_variable

| 操作 | 锁类型 | 说明 |
|---|---|---|
| `getConn()` | `unique_lock` + `cond_.wait()` | 队列空时阻塞，有归还时被唤醒 |
| `freeConn()` | `lock_guard` + `cond_.notify_one()` | 归还后唤醒一个等待者 |
| `closePool()` | `lock_guard` + `cond_.notify_all()` | 关闭时唤醒所有等待者令其退出 |

### 3. 连接检活 — mysql_ping

每次取出连接时调用 `mysql_ping()`，配合 `MYSQL_OPT_RECONNECT` 选项，若连接已断开则自动重连，对业务层透明。

### 4. 幂等关闭 — atomic exchange

```cpp
if (closed_.exchange(true)) return; // 多次调用 closePool() 只执行一次
```

---

## 四、数据表 Schema

| 表名 | 核心字段 | 用途 |
|---|---|---|
| **User** | `id`, `username`, `password`, `nickname` | 账号鉴权 |
| **Friend** | `userid`, `friendid`, `create_time` | 双向好友关系 |
| **OfflineMessage** | `id`, `to_userid`, `from_userid`, `content`, `send_time` | 离线消息暂存 |

建表脚本: `mysql/init_db.sql`
