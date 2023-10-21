#pragma once
#define STACK_INFO_SIZE 4096
#include "Windows.h"

class tArray {
private:
    struct Node {
        void* data;
        Node* next;
    };

    Node* head;
    Node* tail; 
    int size;

public:
    tArray() {
        head = nullptr;
        size = 0;
    }

    ~tArray() {
        clear();
    }

    void push(void* value) {
        Node* newNode = (Node*)malloc(sizeof(Node));
        newNode->data = value;
        newNode->next = nullptr;

        if (head == nullptr) {
            head = newNode;
            tail = newNode; // 如果链表为空，头尾指针都指向新节点
        }
        else {
            tail->next = newNode; // 将新节点插入到尾节点之后
            tail = newNode; // 更新尾指针为新节点
        }

        size++;
    }

    void erase(void* value) {
        Node* current = head;
        Node* prev = nullptr;

        while (current != nullptr) {
            if (current->data == value) {
                if (prev == nullptr) {
                    head = current->next;
                }
                else {
                    prev->next = current->next;
                }
                free(current);
                size--;
                return;
            }
            prev = current;
            current = current->next;
        }
    }

    void clear() {
        Node* current = head;
        while (current != nullptr) {
            Node* temp = current;
            current = current->next;
            free(temp);
        }
        head = nullptr;
        size = 0;
    }

    int getSize() {
        return size;
    }
};

struct MEM_ALLOC_INFO {
    void* stack[10];
    void* ptr;
    size_t size;
    MEM_ALLOC_INFO();
};


struct MEM_ALLOC_STATIS {
	int stackID;
	void* stack[10];
	size_t allocCount;
	size_t allocSize;
    tArray buffList;
	MEM_ALLOC_STATIS();
};
extern bool g_enableMemDiag;
#define MAX_STACK_COUNT 1000
extern MEM_ALLOC_STATIS* g_memAlloc[MAX_STACK_COUNT];
extern void* operator new(size_t size);
extern void operator delete(void* ptr);


class MemDiag {
public:
    MemDiag();
};