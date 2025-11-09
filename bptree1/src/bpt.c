#include "bpt.h"

H_P *hp;

page *rt = NULL; // root is declared as global

int fd = -1; // fd is declared as global. 파일 디스크립터. .db 데이터 파일을 간접적으로 가리키는 논리적인 포인터

H_P *load_header(off_t off) // 디스크에 있는 B+ Tree 데이터 파일에서 헤더 페이지의 정보를 읽어와 메모리에 로드할 때 사용되는 함수
{
    H_P *newhp = (H_P *)calloc(1, sizeof(H_P));
    if (sizeof(H_P) > pread(fd, newhp, sizeof(H_P), 0)) // 다 읽어오지 못한경우
    {
        // pread(int fd, void *buf, size_t count, off_t offset) : 파일 디스크립터를 사용하여 디스크 파일에서 데이터를 읽는다.
        //                  fd, 읽은 데이터를 저장할 메모리 버퍼, 읽을 최대 바이트 수, .db 파일 내에서 읽기를 시작할 오프셋(위치)
        return NULL;
    }
    return newhp;
}

page *load_page(off_t off) // 디스크에 있는 B+ Tree 데이터 파일에서 특정 위치(오프셋)에 있는 일반 페이지(노드) 하나를 읽어와 메모리에 로드하는 함수
{
    page *load = (page *)calloc(1, sizeof(page));
    // if (off % sizeof(page) != 0) printf("load fail : page offset error\n");
    if (sizeof(page) > pread(fd, load, sizeof(page), off))
    {

        return NULL;
    }
    return load;
}

int open_table(char *pathname) // 시작 함수
{
    fd = open(pathname, O_RDWR | O_CREAT | O_EXCL | O_SYNC, 0775);
    hp = (H_P *)calloc(1, sizeof(H_P));
    if (fd > 0) // 새 파일 생성 및 초기화
    {
        // printf("New File created\n");
        hp->fpo = 0;          // 처음 시작할때는 free 페이지 없음
        hp->num_of_pages = 1; // 처음 시작할때는 헤더 페이지만 존재
        hp->rpo = 0;          // 처음 시작할때는 root 페이지 없음

        pwrite(fd, hp, sizeof(H_P), 0);
        // pwrite(int fd, const void *buf, size_t count, off_t offset) : 파일 디스크립터를 사용하여 파일에 데이터를 쓴다.
        //                  fd, 파일에 쓸 데이터가 저장된 메모리 버퍼, 쓸 바이트 수, .db 파일 내에서 쓰기 시작할 오프셋(위치)
        free(hp);
        hp = load_header(0); // 방금 디스크에 저장한 최신 정보를 다시 읽어옴
        return 0;
    }

    fd = open(pathname, O_RDWR | O_SYNC);
    if (fd > 0) // 기존 파일 열기 및 로드
    {
        // printf("Read Existed File\n");
        if (sizeof(H_P) > pread(fd, hp, sizeof(H_P), 0))
        {
            return -1;
        }
        off_t r_o = hp->rpo;
        rt = load_page(r_o);
        return 0;
    }
    else
        return -1;
}

void reset(off_t off) // 디스크의 새로운 공간에 B+ Tree 노드(페이지)로서 필요한 초기 메타데이터를 설정해주는 역할
{
    page *reset;
    reset = (page *)calloc(1, sizeof(page));
    reset->parent_page_offset = 0;
    reset->is_leaf = 0;
    reset->num_of_keys = 0;
    reset->next_offset = 0;

    pwrite(fd, reset, sizeof(page), off);
    free(reset);
    return;
}

void freetouse(off_t fpo) // 이미 파일 내에 확보된 기존 Free Page를 B+ Tree 노드로 재사용하기 위해 초기화
{
    page *reset;
    reset = load_page(fpo);
    reset->parent_page_offset = 0;
    reset->is_leaf = 0;
    reset->num_of_keys = 0;
    reset->next_offset = 0;

    pwrite(fd, reset, sizeof(page), fpo);
    free(reset);
    return;
}

// B+ Tree에서 더이상 사용하지 않는 노드를 Free Page List의 맨 앞쪽에 연결하여 재사용 할 수 있도록 함. (스택 방식)
// 이 변경사항을 헤더 페이지에 반영. wbf : want to be free
void usetofree(off_t wbf)
{
    page *utf = load_page(wbf);
    utf->parent_page_offset = hp->fpo; // utf가 free 페이지 리스트 맨 앞에 옴
    utf->is_leaf = 0;
    utf->num_of_keys = 0;
    utf->next_offset = 0;

    // 페이지 해제 및 free 페이지 리스트로 write
    pwrite(fd, utf, sizeof(page), wbf); // fd, 파일에 쓸 데이터가 저장된 메모리 버퍼, 쓸 바이트 수, .db 파일 내에서 쓰기 시작할 오프셋(위치)
    free(utf);

    // Header Page 갱신
    hp->fpo = wbf;
    pwrite(fd, hp, sizeof(hp), 0); // 변경된 Header Page를 디스크의 오프셋 0에 새로 기록해서 새로운 Free Page List의 시작점을 반영한다.
    free(hp);
    hp = load_header(0);
    return;
}

off_t new_page() // B+ Tree에서 새로운 노드(페이지)가 필요할 때, 디스크 파일 내에 4096 Bytes의 새로운 페이지 공간을 할당하고 초기화하는 역할
{
    off_t newp;
    page *np;
    off_t prev;
    if (hp->fpo != 0) // free page가 있다면 재사용
    {
        newp = hp->fpo;
        np = load_page(newp); // 메모리로 load

        hp->fpo = np->parent_page_offset; // free 페이지 리스트에서 np 제거
        pwrite(fd, hp, sizeof(hp), 0);    // 변경된 Header Page 정보를 디스크에 반영
        free(hp);
        hp = load_header(0);

        free(np);
        freetouse(newp);
        return newp;
    }

    // 파일 포인터를 파일의 끝으로 이동시키고, 파일 끝의 새로운 오프셋을 저장
    newp = lseek(fd, 0, SEEK_END); // lseek(int fd, off_t offset, int whence) : fd를 이동시키는 함수
                                   // fd, whence 기준으로 이동할 바이트 수, 오프셋 계산 기준점

    // if (newp % sizeof(page) != 0) printf("new page made error : file size error\n");
    reset(newp);
    hp->num_of_pages++;

    pwrite(fd, hp, sizeof(H_P), 0);
    free(hp);
    hp = load_header(0);

    return newp;
}

int cut(int length)
{
    if (length % 2 == 0)
        return length / 2;
    else
        return length / 2 + 1;
}

void start_new_file(record rec) // 디스크 파일 내에 최초의 B+ Tree 구조를 생성하는 역할
{
    page *root; // ?
    off_t ro;
    ro = new_page();
    rt = load_page(ro);
    hp->rpo = ro;

    pwrite(fd, hp, sizeof(H_P), 0); // hp->rpo가 반영된 새로운 헤더 페이지를 적용
    free(hp);
    hp = load_header(0);

    rt->num_of_keys = 1;
    rt->is_leaf = 1;
    rt->records[0] = rec;

    pwrite(fd, rt, sizeof(page), hp->rpo);
    free(rt);
    rt = load_page(hp->rpo);
    // printf("new file is made\n");
}

char *db_find(int64_t key)
{
    page *c = load_page(hp->rpo); // root 노드
    while (c->is_leaf != 1)
    {
        off_t next_c = 0;
        int index = c->num_of_keys;

        int i;
        for (i = 0; i < c->num_of_keys; i++)
        {
            if (key <= c->b_f[i].key) // key보다 크거나 같은 최초의 K i+1를 찾음
                break;
        }

        // next_offset = P1 | (K1, P2) | (K2, P3) | (K3, P4) | | | (Kn, Pn+1)
        // 제일 왼쪽 포인터 P 1는  c->next_offset

        if (i == c->num_of_keys) // key가 모든 값보다 큰 경우. 가장 마지막 포인터로 이동
            next_c = c->b_f[c->num_of_keys - 1].p_offset;
        else if (i == 0) // 첫 번째 탐색에서 멈추는 경우
        {
            if (key < c->b_f[0].key) // key가 K1보다 작은 경우 P1으로 이동
                next_c = c->next_offset;
            else
                next_c = c->b_f[0].p_offset; // key == k1인 경우
        }
        else
            next_c = c->b_f[i].p_offset;

        // 이전 페이지 메모리 해제
        page *prev_c = c;
        c = load_page(next_c);
        free(prev_c);
    }

    // leaf 노드 도착
    char *tmp = NULL;
    for (int i = 0; i < c->num_of_keys; i++)
    {
        if (key == c->records[i].key)
        {
            tmp = (char *)malloc(sizeof(c->records[i].value));
            strcpy(tmp, c->records[i].value);
            break;
        }
    }
    free(c);
    return tmp;
}

int db_insert(int64_t key, char *value)
{
}

int db_delete(int64_t key)
{

} // fin
