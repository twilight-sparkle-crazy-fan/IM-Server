-- ============================================================
-- IM Server 数据库初始化脚本
-- 使用方法: mysql -u root -p < init_db.sql
-- ============================================================

CREATE DATABASE IF NOT EXISTS im_server
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_general_ci;

USE im_server;

-- 1. 用户表
CREATE TABLE IF NOT EXISTS User (
    id          INT             PRIMARY KEY AUTO_INCREMENT   COMMENT '用户唯一标识 (UID)',
    username    VARCHAR(50)     UNIQUE NOT NULL              COMMENT '登录账号',
    password    VARCHAR(64)     NOT NULL                     COMMENT '密码（建议存储哈希值）',
    nickname    VARCHAR(50)     DEFAULT NULL                 COMMENT '用户昵称'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户表';

-- 2. 好友关系表（双向存储：A->B 和 B->A 各一条记录）
CREATE TABLE IF NOT EXISTS Friend (
    userid      INT             NOT NULL                    COMMENT '用户 ID',
    friendid    INT             NOT NULL                    COMMENT '好友 ID',
    create_time TIMESTAMP       DEFAULT CURRENT_TIMESTAMP   COMMENT '成为好友的时间',
    PRIMARY KEY (userid, friendid),
    INDEX idx_userid (userid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='好友关系表';

-- 3. 离线消息表
CREATE TABLE IF NOT EXISTS OfflineMessage (
    id          INT             PRIMARY KEY AUTO_INCREMENT   COMMENT '消息流水 ID',
    to_userid   INT             NOT NULL                    COMMENT '接收者 ID',
    from_userid INT             NOT NULL                    COMMENT '发送者 ID',
    content     TEXT            NOT NULL                    COMMENT '消息内容（JSON 字符串）',
    send_time   TIMESTAMP       DEFAULT CURRENT_TIMESTAMP   COMMENT '存表时间',
    INDEX idx_to_userid (to_userid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='离线消息表';
