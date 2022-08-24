# TDS - 物联网数据服务

## 基于JSON的NoSql时序数据库

### 基本概念

    TDS面向物联网场景设计，使用一个 **"位号"**(tag) 来存储来自于某一个设备或者是某一个传感器的时序数据。可以将一个 "位号" 理解为关系型数据库中的一张表，或者理解为NoSql数据库如MongoDB中的一个Collection。

    来自设备的一次数据采集在TDS中称为一个 **"数据元"**,一个数据元就是一条数据记录。

| 关系型数据库 | MongoDB         | TDS                |
| ------ | --------------- | ------------------ |
| 数据库    | 数据库             | 数据库                |
| 表      | 集合 (Collection) | 位号 (Tag)           |
| 行      | 文档 (Document)   | 数据元 (Data Element) |
| 列      | JSON文档的字段       | JSON的字段            |

### 开发接口

    使用HTTP API接口。HTTP的Body部分为基于JsonRPC的请求与响应。

    所有的数据插入或者查询都基于JSON格式。

    [数据库接口API文档- 点击查看](https://www.liangtusoft.com/doc/api.html#/api-db)

    [Postman API 测试用例 - 点击查看](https://www.postman.com/planetary-sunset-285884/workspace/tds-api)

### 快速开始

   [最新版本下载链接](http://www.liangtusoft.com/release)

   [3分钟快速体验 - 点击播放视频](https://www.bilibili.com/video/BV1zS4y1s7KF?share_source=copy_web&vd_source=4711b38a4edbc7f25c4a98d4c399e893)

### 高性能

针对物联网时序数据场景优化，可以比mysql有更快的读取速度。    
对比测试报告：http://www.liangtusoft.com/doc/#/tds-vs-mysql

![banner image](http://www.liangtusoft.com/assets/tds_vs_mysql.png)

### 存储格式

数据库基于json文件进行存储，在磁盘上，通过时间，位号组织成特定的目录结构。
数据库的目录结构和数据文件都是直接可以阅读和操作的。

**数据元结构**

| 数据元属性    |                                       |
| -------- | ------------------------------------- |
| time     | 数据采集的时间。可以包含年月日或者不包含年月日               |
| tag      | 数据来源的位号。可省略                           |
| dataFile | 关联的二进制数据信息，如视频，图片等                    |
| val      | 数据的值，可以有 整形，浮点型，布尔型，字符串型和json格式的自定义类型 |

## 物联网组态软件

    TDS不仅仅是数据库，还具有监控对象管理功能，硬件设备接入管理功能，内存数据库功能。共同组成了一个物联网组态软件。

   可以实现硬件设备接入，历史数据查询，实时数据查询等物联网监控功能。

   [使用透传网关接入Modbus设备 - 点击播放视频](https://www.bilibili.com/video/BV1sV4y1W7PX/?vd_source=3b5c83dc2775dbd078466c5fe0eed7de)

   了解更多功能: http://www.liangtusoft.com/doc/#/

   下载试用: [www.liangtusoft.com/release](http://www.liangtusoft.com/release)

   硬件接入部分功能尚未开源，专业版提供50个IO点免费版本
