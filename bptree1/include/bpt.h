#ifndef __BPT_H__
#define __BPT_H__

// Uncomment the line below if you are compiling on Windows.
// #define WINDOWS
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#define LEAF_MAX 31
#define INTERNAL_MAX 248

typedef struct record // leaf 페이지에서 사용되는 실제 데이터 레코드. 총 128 Bytes
{
    int64_t key;     // 8 Bytes
    char value[120]; // 120 Bytes
} record;

// next_offset = P1 | (K1, P2) | (K2, P3) | (K3, P4) | | | (Kn, Pn+1)
typedef struct inter_record // internal 페이지에서 사용되는 하위 페이지를 가리키는 인덱스 레코드. 총 16 Bytes
{
    int64_t key;    // 8 Bytes
    off_t p_offset; // 8 Bytes.
                    // Page Offset(포인터). Key <= 의 데이터가 저장되어 있는 하위 페이지의 디스크 위치(오프셋)를 가리킨다.
} I_R;

typedef struct Page // 1 node(page) = 4096 Bytes (4 KB)
{
    // 페이지 헤더 (128 Bytes)
    off_t parent_page_offset; // Parent Page Number: 부모 페이지의 위치(오프셋) 저장. [0, 7].
                              // + 만약 페이지 구조체가 free 페이지로 사용되는 경우는 다음 free 페이지의 오프셋이 저장된다.
    int is_leaf;              // 1: leaf page, 0 : internal page. [8, 11]
    int num_of_keys;          // 현재 페이지에 들어있는 키(레코드)의 개수. [12, 15]
    char reserved[104];       // [16, 119]

    off_t next_offset; // leaf일 때 : Right Sibling Page Number. [120, 127]
                       // 디스크 상의 다음 leaf 페이지 위치(오프셋)를 저장해서 leaf 노드를 순차적으로 연결함.
                       // (가장 오른쪽 리프 페이지는 값이 0)
                       // internal일 때 : One More Page Number.
                       // 인덱스 엔트리보다 하나 더 많은 포인터가 필요한 내부 페이지에서,
                       // 가장 왼쪽 하위 페이지의 위치(오프셋)를 저장하는 데 사용됨.

    // 페이지 바디 (3968 Bytes)
    union // leaf인지 internal이 정해지면 하나의 역할만 수행
    {
        I_R b_f[248];       // internal일 때, 하위 페이지를 가리키는 인덱스 저장. 레코드 총 248개 저장 가능. 248 * 16 = 3968 Bytes
        record records[31]; // leaf일 때, 실제 데이터 레코드 저장. 레코드 총 31개 저장 가능. 31 * 128 Bytes = 3968 Bytes
    };
} page;

typedef struct Header_Page // B+ Tree 파일 전체의 메타데이터. 데이터 파일의 첫 번째 페이지이다.
{
    off_t fpo;            // 다음에 사용할 수 있는 Free Page의 페이지 번호(offset). Free Page List의 첫 번째 페이지를 가리킨다. [0, 7]
    off_t rpo;            // Root Page의 페이지 번호(offset). 트리의 시작점이다. [8, 15]
    int64_t num_of_pages; // 현재 데이터 파일에 할당된 페이지의 개수. [16, 23]
    char reserved[4072];  // [24, 4095]
} H_P;

extern int fd;

extern page *rt;

extern H_P *hp;

// FUNCTION PROTOTYPES.
int open_table(char *pathname);
H_P *load_header(off_t off);
page *load_page(off_t off);

void reset(off_t off);
off_t new_page();
void freetouse(off_t fpo);
int cut(int length);
int parser();
void start_new_file(record rec);

char *db_find(int64_t key);
int db_insert(int64_t key, char *value);
int db_delete(int64_t key);

#endif /* __BPT_H__*/
