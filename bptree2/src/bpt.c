#include "bpt.h"

// flag가 0인 레코드만 따로 저장하는 구조체
typedef struct
{
    record *records;
    int cnt; // 유효 레코드 개수
    int capacity;
} Records;

Records Extract_flag_0();
off_t Construct_leaf(Records *R);
off_t Construct_internal(off_t first_level_offset, int *num_pointers);

H_P *hp;

page *rt = NULL; // root is declared as global

int fd = -1; // fd is declared as global

H_P *load_header(off_t off)
{
    H_P *newhp = (H_P *)calloc(1, sizeof(H_P));
    if (sizeof(H_P) > pread(fd, newhp, sizeof(H_P), 0))
    {

        return NULL;
    }
    return newhp;
}

page *load_page(off_t off)
{
    page *load = (page *)calloc(1, sizeof(page));
    // if (off % sizeof(page) != 0) printf("load fail : page offset error\n");
    if (sizeof(page) > pread(fd, load, sizeof(page), off))
    {

        return NULL;
    }
    return load;
}

int open_table(char *pathname)
{
    fd = open(pathname, O_RDWR | O_CREAT | O_EXCL | O_SYNC, 0775);
    hp = (H_P *)calloc(1, sizeof(H_P));
    if (fd > 0)
    {
        // printf("New File created\n");
        hp->fpo = 0;
        hp->num_of_pages = 1;
        hp->rpo = 0;
        pwrite(fd, hp, sizeof(H_P), 0);
        free(hp);
        hp = load_header(0);
        return 0;
    }
    fd = open(pathname, O_RDWR | O_SYNC);
    if (fd > 0)
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

void reset(off_t off)
{
    page *reset;
    reset = (page *)calloc(1, sizeof(page));
    reset->parent_page_offset = 0;
    reset->is_leaf = 0;
    reset->num_of_keys = 0;
    reset->next_offset = 0;
    memset(reset->reserved, 0, sizeof(reset->reserved));
    pwrite(fd, reset, sizeof(page), off);
    free(reset);
    return;
}

void freetouse(off_t fpo)
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

void usetofree(off_t wbf)
{
    page *utf = load_page(wbf);
    utf->parent_page_offset = hp->fpo;
    utf->is_leaf = 0;
    utf->num_of_keys = 0;
    utf->next_offset = 0;
    pwrite(fd, utf, sizeof(page), wbf);
    free(utf);
    hp->fpo = wbf;
    pwrite(fd, hp, sizeof(hp), 0);
    free(hp);
    hp = load_header(0);
    return;
}

off_t new_page()
{
    off_t newp;
    page *np;
    off_t prev;
    if (hp->fpo != 0)
    {
        newp = hp->fpo;
        np = load_page(newp);
        hp->fpo = np->parent_page_offset;
        pwrite(fd, hp, sizeof(hp), 0);
        free(hp);
        hp = load_header(0);
        free(np);
        freetouse(newp);
        return newp;
    }
    // change previous offset to 0 is needed
    newp = lseek(fd, 0, SEEK_END);
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

void start_new_file(record rec)
{

    page *root;
    off_t ro;
    ro = new_page();
    rt = load_page(ro);
    hp->rpo = ro;
    pwrite(fd, hp, sizeof(H_P), 0);
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
    page *c = load_page(hp->rpo);
    while (c->is_leaf != 1)
    {
        off_t next_c = 0;
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

    char *tmp = NULL;
    char *deletion_flags = (char *)c->reserved; // 시작 오프셋 16. reserved 배열을 deletion_flags로 사용.
    for (int i = 0; i < c->num_of_keys; i++)
    {
        if (key == c->records[i].key)
        {
            if (deletion_flags[i] == 1) // flag가 1이면 삭제된 레코드로 간주
                break;

            tmp = (char *)malloc(sizeof(c->records[i].value));
            strcpy(tmp, c->records[i].value);
            break;
        }
    }
    free(c);
    return tmp;
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

        // 논리적 flag 초기화. flag가 0이면 삭제되지 않은 레코드로 간주
        char *deletion_flags = (char *)node->reserved;
        deletion_flags[0] = 0;
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

        // 논리적 flag 초기화
        char *deletion_flags = (char *)node->reserved;
        deletion_flags[index] = 0;
    }

    pwrite(fd, node, sizeof(page), leaf_offset); // 디스크에 저장
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

        now = next_L; // 현재 오프셋 갱신
    }

    // 중복 키 검사
    int duplicate_index = -1;
    for (int i = 0; i < L->num_of_keys; i++)
    {
        if (key == L->records[i].key)
        {
            duplicate_index = i;
            break;
        }
    }
    if (duplicate_index != -1) // 중복 키가 발견된 경우 insert 실행 x
    {
        free(L);
        return 1;
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

        memset(new_node->reserved, 0, sizeof(new_node->reserved));
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

int db_delete(int64_t key)
{
    off_t now = hp->rpo;
    page *L = load_page(now);
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

        now = next_L; // 현재 오프셋 갱신
    }

    // Key가 L에 존재하는지 확인
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

    char *deletion_flags = (char *)L->reserved;
    deletion_flags[delete_index] = 1; // 인덱스 플래그를 1로만 설정
    pwrite(fd, L, sizeof(page), now);

    free(L);
    return 0; // 삭제 마킹 성공
}

Records Extract_flag_0()
{
    Records R;
    R.capacity = 10000; // 초기 용량 설정
    R.records = calloc(R.capacity, sizeof(record));
    R.cnt = 0;

    // 레코드 할당 실패 시
    if (R.records == NULL)
    {
        R.capacity = 0;
        return R;
    }

    off_t root = hp->rpo; // 루트에서 시작
    if (root == 0)
    {
        free(R.records);
        R.records = NULL;
        R.capacity = 0;
        return R;
    }

    page *c = load_page(root);
    while (c->is_leaf != 1) // 트리의 가장 왼쪽 leaf 노드를 찾는다.
    {
        off_t next_offset = c->next_offset; // c가 internal일때는 next_offset이 P1을 가리킨다.

        page *prev_c = c;
        c = load_page(next_offset);
        free(prev_c);
    }

    while (c != NULL)
    {
        char *deletion_flags = (char *)c->reserved;

        for (int i = 0; i < c->num_of_keys; i++)
        {
            if (deletion_flags[i] == 0) // flag가 0인 레코드만 저장
            {
                R.records[R.cnt++] = c->records[i];
            }
        }

        off_t next_leaf_offset = c->next_offset; // 다음 leaf 노드로 이동
        free(c);

        if (next_leaf_offset == 0)
            c = NULL; // 마지막 리스트
        else
            c = load_page(next_leaf_offset);
    }

    return R;
}

off_t Construct_leaf(Records *R)
{
    off_t first_leaf_offset = 0; // 가장 왼쪽 leaf인 경우
    off_t prev_leaf_offset = 0;
    int leaf_max_cnt = 31; // 한 leaf의 최대 레코드 수

    for (int i = 0; i < R->cnt; i += leaf_max_cnt) // 노드 순회
    {
        off_t new_leaf_offset = new_page();
        page *new_leaf = load_page(new_leaf_offset);
        new_leaf->is_leaf = 1;
        new_leaf->parent_page_offset = 0;
        memset(new_leaf->reserved, 0, sizeof(new_leaf->reserved));

        int records_to_copy = leaf_max_cnt;
        if (i + leaf_max_cnt > R->cnt) // 마지막 leaf 노드일때
            records_to_copy = R->cnt - i;

        for (int j = 0; j < records_to_copy; j++) // 레코드 순회
        {
            new_leaf->records[j] = R->records[i + j]; // 구조체 대입
        }
        new_leaf->num_of_keys = records_to_copy;

        // leaf 노드 연결
        if (first_leaf_offset == 0)
        {
            first_leaf_offset = new_leaf_offset;
        }
        else
        {
            page *prev_leaf = load_page(prev_leaf_offset);
            prev_leaf->next_offset = new_leaf_offset;
            pwrite(fd, prev_leaf, sizeof(page), prev_leaf_offset);
            free(prev_leaf);
        }

        new_leaf->next_offset = 0; // 마지막 leaf일 경우 0으로 저장됨
        pwrite(fd, new_leaf, sizeof(page), new_leaf_offset);
        free(new_leaf);

        prev_leaf_offset = new_leaf_offset;
    }

    return first_leaf_offset;
}

off_t Construct_internal(off_t first_level_offset, int *num_pointers)
{
    int internal_max_cnt = 248; // 한 internal의 최대 레코드 개수

    off_t current_offset = first_level_offset;
    page *current_node = load_page(current_offset);
    if (current_node == NULL) // 로드 실패 시
    {
        *num_pointers = 0;
        return 0;
    }

    // current의 부모 노드 생성
    off_t new_internal_offset = new_page();
    page *new_internal = load_page(new_internal_offset);
    if (new_internal == NULL) // 로드 실패 시
    {
        *num_pointers = 0;
        return 0;
    }
    new_internal->is_leaf = 0;
    new_internal->num_of_keys = 0;
    new_internal->parent_page_offset = 0;
    new_internal->next_offset = current_offset; // P1에 current_node를 지정

    // 새로운 상위 레벨 구축을 위한 변수
    off_t first_internal_offset = 0; // 새 레벨의 가장 왼쪽 노드 오프셋
    off_t prev_internal_offset = 0;
    int now_cnt = 0; // 이 레벨에 생성된 internal 노드 개수

    while (true)
    {
        int64_t k_prime;
        if (current_node->is_leaf == 1) // 하위 노드가 leaf이면
        {
            k_prime = current_node->records[0].key;
        }
        else
        {
            k_prime = current_node->b_f[0].key; // Internal 노드 (P2에 있는 K1)
        }

        if (new_internal->num_of_keys >= internal_max_cnt) // 부모 internal 노드가 가득 차면 write 후 새 노드 생성
        {
            pwrite(fd, new_internal, sizeof(page), new_internal_offset);
            free(new_internal);

            // 새 internal 노드 생성 (형제 노드)
            if (first_internal_offset == 0)
            {
                first_internal_offset = new_internal_offset; // 새 레벨의 시작점 저장
            }
            else
            {
                // 이전 internal 노드를 next_offset으로 업데이트
                page *prev_internal = load_page(prev_internal_offset);
                if (prev_internal != NULL)
                {
                    prev_internal->next_offset = new_internal_offset;
                    pwrite(fd, prev_internal, sizeof(page), prev_internal_offset);
                    free(prev_internal);
                }
            }
            prev_internal_offset = new_internal_offset;

            new_internal_offset = new_page();
            new_internal = load_page(new_internal_offset);
            new_internal->is_leaf = 0;
            new_internal->num_of_keys = 0;
            new_internal->parent_page_offset = 0;
            new_internal->next_offset = current_offset; // P1 설정 (하위 노드의 현재 위치)

            now_cnt++; // 생성된 Internal 노드 개수 카운트
        }

        // K' 삽입
        new_internal->b_f[new_internal->num_of_keys].key = k_prime;
        new_internal->b_f[new_internal->num_of_keys].p_offset = current_offset;
        new_internal->num_of_keys++;

        off_t next_lower_offset = current_node->next_offset; // 다음 형제 노드로 이동

        // current 노드의 부모 업데이트
        current_node->parent_page_offset = new_internal_offset;
        pwrite(fd, current_node, sizeof(page), current_offset);
        free(current_node);

        if (next_lower_offset == 0)
        {
            break; // current 노드의 끝
        }
        current_offset = next_lower_offset;
        current_node = load_page(current_offset);
        if (current_node == NULL) // 하위 노드 로드 실패 시 루프 종료
            break;
    }

    // 최종 부모 internal 노드를 디스크에 반영
    pwrite(fd, new_internal, sizeof(page), new_internal_offset);
    free(new_internal);

    if (first_internal_offset == 0) // internal이 한개이면
    {
        *num_pointers = 1;
        return new_internal_offset; // 새로 생성된 노드의 오프셋을 Root로 반환
    }
    else
    {
        *num_pointers = now_cnt + 1;  // 생성된 internal 노드 + 자기 자신
        return first_internal_offset; // 새 레벨의 시작 오프셋 반환
    }
}

void Garbage_Collector_recursive(off_t node_offset)
{
    if (node_offset == 0)
        return;

    page *n = load_page(node_offset);
    if (n == NULL)
        return;

    if (n->is_leaf == 0) // internal인 경우 재귀
    {
        Garbage_Collector_recursive(n->next_offset); // P1로 재귀

        // P2 이후로 재귀
        for (int i = 0; i < n->num_of_keys; i++)
        {
            Garbage_Collector_recursive(n->b_f[i].p_offset);
        }
    }

    free(n);                // 현재 노드 메모리 해제
    usetofree(node_offset); // free page로 반환
}

void db_reorganize()
{
    if (hp == NULL) // hp 유효 체크
        return;

    Records R = Extract_flag_0(); // flag가 0인 레코드만 가져오기
    if (R.cnt == 0)
    {
        free(R.records);
        return;
    }

    // 이전 버전은 청소
    off_t old_root_offset = hp->rpo;
    Garbage_Collector_recursive(old_root_offset);

    // 새로운 트리 구축
    hp->rpo = 0;
    off_t current_level_offset = Construct_leaf(&R); // Leaf 레벨 구축 완료

    off_t new_root_offset = 0;
    int node_cnt = 0; // 부모의 internal 노드 개수

    while (true)
    {
        off_t next_level_offset = Construct_internal(current_level_offset, &node_cnt); // 부모 레벨의 시작 오프셋

        if (node_cnt == 1) // 노드가 1개면 루트
        {
            new_root_offset = next_level_offset;
            break;
        }

        // 다음 레벨 구축을 위해 현재 레벨 오프셋을 업데이트
        current_level_offset = next_level_offset;
    }

    hp->rpo = new_root_offset; // 최종 루트
    pwrite(fd, hp, sizeof(H_P), 0);

    free(hp);
    hp = load_header(0);

    free(R.records);
}

// fin
