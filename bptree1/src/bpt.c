#include "bpt.h"

off_t n_prime_info(off_t n_offset, int64_t *k_prime_ptr, off_t *n_prime_offset_ptr, int *k_prime_idx_ptr);
void Coalesce_OR_Redistribution(page *n, off_t n_offset, off_t parent_offset, int64_t k_prime, off_t n_prime_offset, int k_prime_idx);

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
    // root NULL 체크
    if (hp == NULL || hp->rpo == 0)
        return NULL;

    page *c = load_page(hp->rpo); // root 노드
    if (c == NULL)
        return NULL; // 로드 실패시
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

        if (c == NULL) // 로드 실패시
            return NULL;
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

void insert_in_leaf(page *node, int64_t key, char *value, off_t leaf_offset)
{
    record newRecord;
    newRecord.key = key;
    strcpy(newRecord.value, value);

    if (key < node->records[0].key)
    {
        for (int i = node->num_of_keys - 1; i >= 0; i--)
        {
            node->records[i + 1] = node->records[i];
        }
        node->records[0] = newRecord;
        node->num_of_keys++;
    }
    else
    {
        int index = node->num_of_keys;
        for (int i = 0; i < node->num_of_keys; i++)
        {
            if (node->records[i].key > key)
            {
                index = i;
                break;
            }
        }
        for (int i = node->num_of_keys - 1; i >= index; i--)
        {
            node->records[i + 1] = node->records[i];
        }
        node->records[index] = newRecord;
        node->num_of_keys++;
    }

    pwrite(fd, node, sizeof(page), leaf_offset); // 디스크에 저장
}

void insert_in_parent(off_t now_node_offset, int64_t k, off_t new_node_offset)
{
    if (hp->rpo == now_node_offset)
    {
        off_t new_root_offset = new_page();
        page *R = load_page(new_root_offset); // 새로운 루트 페이지 생성

        R->is_leaf = 0;
        R->num_of_keys = 1;
        R->parent_page_offset = 0;

        R->b_f[0].key = k;
        R->next_offset = now_node_offset; // N k N'
        R->b_f[0].p_offset = new_node_offset;

        off_t old_root_offset = hp->rpo;
        hp->rpo = new_root_offset;      // hp 바꾸고
        pwrite(fd, hp, sizeof(H_P), 0); // 저장
        free(hp);
        hp = load_header(0);

        pwrite(fd, R, sizeof(page), new_root_offset); // 디스크에 저장
        free(R);

        rt = load_page(new_root_offset);
        return;
    }

    page *p = load_page(now_node_offset);
    off_t parent_offset = p->parent_page_offset;
    free(p);

    page *parent_page = load_page(parent_offset); // 현재 노드의 부모 호출

    if (parent_page->num_of_keys < 248) // 부모 노드에 자리가 있는 경우
    {
        int index = parent_page->num_of_keys;
        for (int i = 0; i < index; i++)
        {
            if (parent_page->b_f[i].key > k) // 들어가야 될 자리 탐색
            {
                index = i;
                break;
            }
        }

        for (int i = parent_page->num_of_keys; i > index; i--)
            parent_page->b_f[i] = parent_page->b_f[i - 1];

        I_R n;
        n.key = k;
        n.p_offset = new_node_offset;
        parent_page->b_f[index] = n;
        parent_page->num_of_keys++;

        pwrite(fd, parent_page, sizeof(page), parent_offset); // 디스크 반영
        free(parent_page);
    }
    else // 부모 노드도 꽉 찬 경우 Split
    {
        I_R T[249]; // 기존 248개 + 새로운 1개

        I_R n;
        n.key = k;
        n.p_offset = new_node_offset;

        for (int i = 0; i < 248; i++)
            T[i] = parent_page->b_f[i];
        int tmp = 248;
        for (int i = 0; i < 248; i++)
        {
            if (T[i].key > k)
            {
                tmp = i;
                break;
            }
        }
        for (int i = 248; i > tmp; i--)
        {
            T[i] = T[i - 1];
        }
        T[tmp] = n;

        off_t p_prime_offset = new_page(); // 새로운 노드 P' 생성
        page *p_prime = load_page(p_prime_offset);
        p_prime->is_leaf = 0;
        p_prime->parent_page_offset = parent_offset;

        int split_cnt = 124;
        int64_t k_prime_prime = T[split_cnt].key; // K'

        p_prime->next_offset = T[split_cnt].p_offset; // P'의 P1 설정

        for (int i = 125; i < 249; i++) // P' 값 설정
        {
            p_prime->b_f[i - 125] = T[i];
        }

        parent_page->num_of_keys = split_cnt;

        pwrite(fd, parent_page, sizeof(page), parent_offset); // 디스크 반영
        pwrite(fd, p_prime, sizeof(page), p_prime_offset);
        free(p_prime);
        free(parent_page);

        insert_in_parent(parent_offset, k_prime_prime, p_prime_offset); // 재귀 호출
    }
}

int db_insert(int64_t key, char *value)
{
    if (hp->rpo == 0) // 트리가 비었을때
    {
        record n;
        n.key = key;
        strcpy(n.value, value);

        start_new_file(n);
        return 0;
    }

    // key가 들어갈 리프 노드 탐색
    off_t now = hp->rpo;
    page *L = load_page(now);
    if (L == NULL) // root 로드 실패 시
        return 1;
    while (L->is_leaf != 1)
    {
        off_t next_L = 0;
        int index = L->num_of_keys;

        int i;
        for (i = 0; i < L->num_of_keys; i++)
        {
            if (key <= L->b_f[i].key) // key보다 크거나 같은 최초의 K i+1를 찾음
                break;
        }

        if (i == L->num_of_keys) // key가 모든 값보다 큰 경우. 가장 마지막 포인터로 이동
            next_L = L->b_f[L->num_of_keys - 1].p_offset;
        else if (i == 0) // 첫 번째 탐색에서 멈추는 경우
        {
            if (key < L->b_f[0].key) // key가 K1보다 작은 경우 P1으로 이동
                next_L = L->next_offset;
            else
                next_L = L->b_f[0].p_offset; // key == k1인 경우
        }
        else
            next_L = L->b_f[i].p_offset;

        // 이전 페이지 메모리 해제
        page *prev_c = L;
        L = load_page(next_L);
        free(prev_c);

        if (L == NULL) // 로드 실패 시
            return 1;
        now = next_L; // 현재 오프셋 갱신
    }

    if (L->num_of_keys < 31)
        insert_in_leaf(L, key, value, now);
    else // 공간이 부족하면 split
    {
        off_t new_node_offset = new_page();
        page *new_node = load_page(new_node_offset); // 새로운 leap 노드 할당

        record T[32]; // 임시 보관. 기존 31개 + 새로 삽입될 1개
        for (int i = 0; i < 31; i++)
        {
            T[i] = L->records[i];
        }
        record t; // 삽입 할 노드
        t.key = key;
        strcpy(t.value, value);
        int index = 31;
        for (int i = 0; i < 31; i++)
        {
            if (T[i].key > t.key)
            {
                index = i;
                break;
            }
        }
        for (int i = 31; i > index; i--)
        {
            T[i] = T[i - 1];
        }
        T[index] = t;

        new_node->next_offset = L->next_offset;
        L->next_offset = new_node_offset; // L -> new_node (L')

        int split_cnt = cut(32);
        L->num_of_keys = split_cnt; // 기존 노드 개수 절반으로 줄이기

        new_node->is_leaf = 1;
        new_node->num_of_keys = split_cnt;

        for (int i = 16; i <= 31; i++)
        {
            new_node->records[i - 16] = T[i];
        }

        int64_t k = new_node->records[0].key; // 부모로 올릴 key. 새로운 노드에서 제일 작은 값

        // 수정된 두 노드를 디스크에 쓰기
        pwrite(fd, L, sizeof(page), now);
        pwrite(fd, new_node, sizeof(page), new_node_offset);
        free(L);
        free(new_node);

        insert_in_parent(now, k, new_node_offset); // L과 L' 오프셋 넘겨주기
    }
}

void delete_entry(off_t now_node_offset, int64_t k)
{
    // 일단 지우기
    page *n = load_page(now_node_offset);
    int index;
    for (int i = 0; i < n->num_of_keys; i++)
    {
        if (n->records[i].key == k)
        {
            for (int j = i; j < n->num_of_keys - 1; j++)
            {
                n->records[j] = n->records[j + 1];
            }
            n->num_of_keys--;
            break;
        }
    }
    pwrite(fd, n, sizeof(page), now_node_offset); // 수정된 리프 노드 저장

    if (n->parent_page_offset == 0) // N이 루트이고, 리프 노드인데
    {
        if (n->num_of_keys == 0) // 키가 하나도 없을때
        {
            usetofree(now_node_offset); // 노드를 완전히 제거. Free 리스트로 반환

            hp->rpo = 0;
            pwrite(fd, hp, sizeof(H_P), 0); // 저장
            free(hp);
            hp = load_header(0);
        }

        free(n);
        return;
    }
    else if (n->num_of_keys < 16) // 언더플로우 처리. (절반보다 작을때)
    {
        int64_t k_prime;
        int k_prime_idx;
        off_t n_prime_offset;

        // N', K' 정보 가져오기
        off_t parent_offset = n_prime_info(now_node_offset, &k_prime, &n_prime_offset, &k_prime_idx);

        if (parent_offset != 0)
        {
            Coalesce_OR_Redistribution(n, now_node_offset, parent_offset, k_prime, n_prime_offset, k_prime_idx); // 병합 또는 재분배 실행
            return;
        }
    }
}

off_t n_prime_info(off_t n_offset, int64_t *k_prime_ptr, off_t *n_prime_offset_ptr, int *k_prime_idx_ptr)
{
    // 현재 노드 N의 부모 읽어오기
    page *n = load_page(n_offset);
    off_t parent_offset = n->parent_page_offset;
    free(n);

    page *p = load_page(parent_offset);
    int n_idx = -1; // 부모에서 N의 인덱스 찾기

    if (p->next_offset == n_offset) // 부모의 p1이 N을 가리키면
    {
        n_idx = -1;
    }
    else
    {
        for (int i = 0; i < p->num_of_keys; i++)
        {
            if (p->b_f[i].p_offset == n_offset)
            {
                n_idx = i;
                break;
            }
        }
    }

    // 형제 노드 N' 지정
    off_t n_prime_offset = 0;
    int k_prime_idx = -1; // K' = N과 N'를 구분하는 부모 노드의 키

    if (n_idx > 0) // N' N인 경우
    {
        n_prime_offset = p->b_f[n_idx - 1].p_offset;
        k_prime_idx = n_idx - 1; // K'은 N' 오른쪽에 있는 키
    }
    else if (n_idx == -1) // N N'인 경우
    {
        n_prime_offset = p->b_f[0].p_offset;
        k_prime_idx = 0; // K'은 P1과 P2 사이의 K1
    }

    // 리턴값 반환
    if (n_prime_offset != 0)
    {
        *k_prime_ptr = p->b_f[k_prime_idx].key;
        *n_prime_offset_ptr = n_prime_offset;
        *k_prime_idx_ptr = k_prime_idx;
    }
    free(p);
    return parent_offset;
}

void Coalesce_OR_Redistribution(page *n, off_t n_offset, off_t parent_offset, int64_t k_prime, off_t n_prime_offset, int k_prime_idx)
{
    // 1. 노드 로드 및 역할 정의 (n_left, n_right)
    page *p = load_page(parent_offset);
    page *n_prime = load_page(n_prime_offset);

    page *n_left, *n_right;
    off_t n_left_offset, n_right_offset;

    // N' N 설정
    n_left = n_prime;
    n_left_offset = n_prime_offset;
    n_right = n;
    n_right_offset = n_offset;

    if (n_left->num_of_keys + n_right->num_of_keys <= 31) // Coalesce
    {
        // n_left <- n_right 복사
        int start = n_left->num_of_keys; // 복사가 시작될 인덱스
        for (int i = 0; i < n_right->num_of_keys; i++)
        {
            n_left->records[start + i] = n_right->records[i];
        }
        n_left->num_of_keys += n_right->num_of_keys;

        n_left->next_offset = n_right->next_offset;
        usetofree(n_right_offset); // n_right 노드 해제

        pwrite(fd, n_left, sizeof(page), n_left_offset); // n_left 변경을 디스크에 반영

        free(n_left);
        free(n_right);
        free(p);

        delete_entry(parent_offset, k_prime); // 재귀 호출로 부모 P에서 K' 제거
        return;
    }
    else // Redistribution
    {
        // n_left -> n_right로 레코드 1개 이동
        for (int i = n_right->num_of_keys; i > 0; i--)
        {
            n_right->records[i] = n_right->records[i - 1];
        }
        n_right->records[0] = n_left->records[n_left->num_of_keys - 1]; // n_left의 마지막 레코드를 n_right의 0번 인덱스로 복사

        n_left->num_of_keys--;
        n_right->num_of_keys++;

        p->b_f[k_prime_idx].key = n_right->records[0].key; // 부모 P의 경계 키 K' 업데이트

        pwrite(fd, n_left, sizeof(page), n_left_offset);
        pwrite(fd, n_right, sizeof(page), n_right_offset);
        pwrite(fd, p, sizeof(page), parent_offset);

        free(n_left);
        free(n_right);
        free(p);
        return;
    }
}

int db_delete(int64_t key)
{
    // root NULL 체크
    if (hp == NULL || hp->rpo == 0)
        return -1;

    // key가 삭제될 리프 노드 탐색
    off_t now = hp->rpo;
    page *L = load_page(now);
    if (L == NULL) // 로드 실패 시
        return -1;
    while (L->is_leaf != 1)
    {
        off_t next_L = 0;
        int index = L->num_of_keys;

        int i;
        for (i = 0; i < L->num_of_keys; i++)
        {
            if (key <= L->b_f[i].key) // key보다 크거나 같은 최초의 K i+1를 찾음
                break;
        }

        if (i == L->num_of_keys) // key가 모든 값보다 큰 경우. 가장 마지막 포인터로 이동
            next_L = L->b_f[L->num_of_keys - 1].p_offset;
        else if (i == 0) // 첫 번째 탐색에서 멈추는 경우
        {
            if (key < L->b_f[0].key) // key가 K1보다 작은 경우 P1으로 이동
                next_L = L->next_offset;
            else
                next_L = L->b_f[0].p_offset; // key == k1인 경우
        }
        else
            next_L = L->b_f[i].p_offset;

        // 이전 페이지 메모리 해제
        page *prev_c = L;
        L = load_page(next_L);
        free(prev_c);

        if (L == NULL) // 로드 실패 시
            return -1;
        now = next_L; // 현재 오프셋 갱신
    }

    int delete_index = -1;
    for (int i = 0; i < L->num_of_keys; i++)
    {
        if (key == L->records[i].key)
        {
            delete_index = i;
            break;
        }
    }

    if (delete_index == -1)
    {
        free(L);
        return -1; // 삭제 실패
    }

    delete_entry(now, key);
    return 0;
} // fin
