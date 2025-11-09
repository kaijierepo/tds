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
    DB_LOCK_GUARD::enable = true;

    thread t1(insert_thread);
    thread t2(read_thread);

    while (1) {
        Sleep(1000);
    }

    return 0;
}

