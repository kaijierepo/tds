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

### CRUD 增删改查操作

#### db.insert 插入数据

**HTTP请求Body**

```json
{
    "jsonrpc": "2.0", 
    "method": "db.insert", 
    "params": {
        "tag":"device001",
        "time":"2022-08-11 08:26:10",
        "val":{
            "temp":24.3,
            "humidity":65.4,
            "pm10":11.3,
            "pm25":22.4
        }
    }, 
    "id": 1
}
```

**HTTP返回Body**

```json
{
    "jsonrpc": "2.0",
    "method": "db.insert",
    "id": 1,
    "result": "ok"
}
```

#### db.select 查询数据

**请求**

```json
{
    "jsonrpc": "2.0", 
    "method": "db.select", 
    "params": {
        "tag":"device001",    //位号
        "time":"10h3m10s"          //查询最近10小时3分钟10秒内的数据
        "filter":"temp>26&&humidity>65"  //查询温度大于25度，湿度小于65的数据
    }, 
    "id": 1
}
```

**响应**

```json
{
    "jsonrpc": "2.0",
    "method": "db.select",
    "id": 1,
    "result": [
        {
            "time": "2022-08-11 11:41:53",
            "val": {
                "humidity": 66.4,
                "pm10": 11.3,
                "pm25": 22.4,
                "temp": 26.3
            }
        },
        {
            "time": "2022-08-11 11:41:57",
            "val": {
                "humidity": 68.4,
                "pm10": 11.3,
                "pm25": 22.4,
                "temp": 27.3
            }
        }
    ]
}
```

#### db.update 更新数据

**请求**

```json
{
    "jsonrpc": "2.0", 
    "method": "db.update", 
    "params": {
        "tag":"device001",
        "time":"2022-08-11 11:41:53",
        "val":{
                "humidity": 66.4,
                "pm10": 11.3456789,
                "pm25": 22.4,
                "temp": 26.3
        }
    }, 
    "id": 1
}
```

**响应**

```json
{
    "jsonrpc": "2.0",
    "method": "db.update",
    "id": 1,
    "result": "ok"
}
```

#### db.delete 删除数据

**请求**

```json
{
    "jsonrpc": "2.0", 
    "method": "db.delete", 
    "params": {
        "tag":"device001",
        "time":"2022-08-11 11:41:53"
    }, 
    "id": 1
}
```

**响应**

```json
{
    "jsonrpc": "2.0",
    "method": "db.delete",
    "id": 1,
    "result": "ok"
}
```

### 基本参数

#### time 时间选择器

| 模式             | 值                                          |                                                           |
| -------------- | ------------------------------------------ | --------------------------------------------------------- |
| 时间点模式          | `2020-02-14 20:20:20`                      | 获得指定时间点的数据元                                               |
| 绝对时间区间模式       | `2020-02-14 20:20:20~2020-02-15 20:20:20`  | 获得数据元列表                                                   |
| 绝对日期区间模式       | `2020-02-14~2020-02-15`                    | 效果同上。对应的具体时间区间为 `2020-02-14 00:00:00~2020-02-15 23:59:59` |
| 相对时间区间模式       | `1d1h1m30s`                                | 选择最近1天1小时1分钟30秒的数据                                        |
|                | `3h`                                       | 选择最近3小时的数据                                                |
| 数据元个数模式        | `3e`                                       | 选择最近的 3个数据元(data element)                                 |
| 多条件模式，使用&&组合条件 | `2020-02-10~2020-02-15&&06:00:00~07:00:00` | 两个时间范围条件同时满足                                              |

#### tag 空间选择器

**选择一个位号**

"tag":"浙江.杭州.滨江.pm25"
精确指定可以忽略根节点

**选择多个位号**

（使用 * 符号,* 符号代表 0-n 个任意字符）

| 格式              | 效果                                                |
| --------------- | ------------------------------------------------- |
| "tag":"*"       | 整个项目所有的监测点                                        |
| "tag":"浙江.杭州.*" | 选中浙江.杭州的子监测点                                      |
| "tag":"浙江.杭州*"  | 可以选中 浙江.杭州南.XXX格式的位号                              |
| "tag":"*.北."    | 可以选中此格式下带一个“北"字的位号 浙江.杭州.下沙.pm25;江苏.徐州.和睦小区.pm25; |

#### match 条件选择器

使用javascript表达式，并且可以使用一些常用的javascript函数。

表达式中的变量是val的属性

例如表达式：

```javascript
"filter":"params.CpHeater > 950 && params.CpObj == 968 && params.comment.indexOf(\"60度\")>=0"
```

indexOf是查找是否包含子字符串的函数。注意字符串中包含 " 符号需要转义。

可以查询到以下数据元：

```json
{
    "time": "2021-09-30 15:15:33",
    "val": {
      "mpType": "TCA3DP",
      "params": {
        "CpHeater": 1000,
        "CpObj": 968,
        "closeFanAheadTime": 10,
        "closeOilBathAfterTest": true,
        "comment": "60度测试"
      },
      "result": {
        "Acr": 8.111388257912006e-07,
        "Ain": 1.0285184798848156e-05
      },
      "type": "json"
    }
  }
```

#### interval 降采样选择器

```json
"interval":3   //每隔3个数据元返回1个数据
```

```json
"interval":"3h10m"  //每隔3小时10分钟返回1个数据
```

### 统计查询

#### group 分组参数

```json
"group":"type"   //指定数据元字段名称分组
```

#### sort 排序参数

```json
"a-sort":"temp"   //ascending sort 指定数据元字段名称升序排列
```

```json
"d-sort":"temp"   //descending sort 指定数据元字段名称降序排列
```

#### limit 分页查询

```json
"limit":10   //返回前10条记录
```

```json
"limit":[5,10]   //第一个参数为offset,第二个为记录条数。返回第6-15条记录
```

### 聚合查询

#### db.count 统计数量

**请求**

```json
{
    "jsonrpc": "2.0", 
    "method": "db.count", 
    "params": {
        "tag":"device001",
        "time":"30h3m4s",
        "match":"temp>26&&humidity>65"
    }, 
    "id": 1
}
```

**响应**

```json
{
    "jsonrpc": "2.0",
    "method": "db.count",
    "id": 1,
    "result": 2
}
```

#### max 参数

```json
"max":"temp"   //选中temp字段最大的数据元
```

#### min 参数

```json
"min":"temp"   //选中min字段最小的数据元
```

#### avg 参数

```json
"avg":"temp"   //temp字段求平均值
```

### 超高性能

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

    了解更多功能: http://www.liangtusoft.com/doc/#/

    下载试用: [www.liangtusoft.com/release](http://www.liangtusoft.com/release)
