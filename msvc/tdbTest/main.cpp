#include "tdb.h"

#include "tdb.h"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include "common.h"

void insert_thread() {
    int i = 0;
    while (1) {
        i++;
        db.Insert("tag1", i);
        Sleep(1);
    }

}

void read_thread() {
    while (1) {
        string params = "{"
            "\"tag\":\"tag1\","
            "\"time\":\"1d\""
            "}";
        DE_SELECTOR sel;
        SELECT_RLT rlt;
        string err;
        if (sel.init(params, err)) {
            db.Select(sel, rlt);
        }
    }
}

int main() {
    string path = fs::appPath() + "/testdb";

    db.Open(path);
    db.m_confPath = fs::appPath() + "/conf";
    DB_LOCK_GUARD::enable = true;

    //读写并发测试
    if (0) {
        thread t1(insert_thread);
        thread t2(read_thread);
    }


    //批量tableUpdate测试
    if (0) {
        //插入十条数据
        for(int i=0;i<10;i++){
            string sDeTpl = R"({"tag":"tag%d","model":"A","count":0})";
			string sDe = str::format(sDeTpl.c_str(), i);
            string err;
            db.tableInsert("table1", sDe,err);
        }
		// 1-5 条数据更新count字段为10的倍数
        vector<string> match;
        vector<string> updateData;
        for (int i = 0; i < 5; i++) {
            string matchStr = str::format(R"(tag=='tag%d')",i);
            string updateStr = str::format(R"({"count":%d})", i * 10);
            match.push_back(matchStr);
            updateData.push_back(updateStr);
        }
        string err;
        if (!db.tableUpdate("table1", match, updateData, err)) {
			cout << "tableUpdate error: " << err << endl;
		}

        db.tableUpdate("table1", "tag=='tag9'", "count++",err);
    }

    //updateCalc测试
    if (1) {
        //插入十条数据
        for (int i = 0; i < 10; i++) {
            string sDeTpl = R"(
                {
                    "tag":"tag%d",
                    "model":"A",
                    "info":{
                        "count":0
                    }
                })";
            string sDe = str::format(sDeTpl.c_str(), i);
            string err;
            db.tableInsert("calcUpdateTest", sDe, err);
        }
        string err;
        db.tableUpdate("calcUpdateTest", "tag=='tag9'", "info.count++", err);
        db.tableUpdate("calcUpdateTest", "tag=='tag9'", "info.count++", err);
        db.tableUpdate("calcUpdateTest", "tag=='tag9'", "info.count++", err);
    }

    while (1) {
        Sleep(1000);
    }

    return 0;
}

