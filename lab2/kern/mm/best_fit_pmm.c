#include <pmm.h>
#include <list.h>
#include <string.h>
#include <default_pmm.h>
#include <buddy_pmm.h>
#include <stdio.h>

#define LEFT_LEAF(index) ((index) * 2 + 1)
#define RIGHT_LEAF(index) ((index) * 2 + 2)
#define PARENT(index) ( ((index) + 1) / 2 - 1)

#define IS_POWER_OF_2(x) (!((x)&((x)-1)))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define UINT32_SHR_OR(a,n)      ((a)|((a)>>(n)))

#define UINT32_MASK(a)          (UINT32_SHR_OR(UINT32_SHR_OR(UINT32_SHR_OR(UINT32_SHR_OR(UINT32_SHR_OR(a,1),2),4),8),16))    
#define UINT32_REMAINDER(a)     ((a)&(UINT32_MASK(a)>>1))
#define UINT32_ROUND_DOWN(a)    (UINT32_REMAINDER(a)?((a)-UINT32_REMAINDER(a)):(a))

static unsigned fixsize(unsigned size) {
    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    size |= size >> 8;
    size |= size >> 16;
    return size + 1;
}

struct buddy2 {
  unsigned size;
  unsigned longest;
};
struct buddy2 root[80000];

free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

struct allocRecord {
  struct Page* base;
  int offset;
  size_t nr;
};

struct allocRecord rec[80000];
int nr_block;

static void buddy_init() {
    list_init(&free_list);
    nr_free = 0;
}

void buddy2_new(int size) {
  unsigned node_size;
  int i;
  nr_block = 0;
  if (size < 1 || !IS_POWER_OF_2(size))
    return;

  root[0].size = size;
  node_size = size * 2;

  for (i = 0; i < 2 * size - 1; ++i) {
    if (IS_POWER_OF_2(i + 1))
        node_size /= 2;
    root[i].longest = node_size;
  }
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page* p = base;
    for (; p != base + n; p++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 1;
        set_page_ref(p, 0);
        SetPageProperty(p);
        list_add_before(&free_list, &(p->page_link));
    }
    nr_free += n;
    int allocpages = UINT32_ROUND_DOWN(n);
    buddy2_new(allocpages);
}

static struct Page*
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free)
        return NULL;
    struct Page* page = NULL;
    struct Page* p;
    list_entry_t *le = &free_list, *temp_le;
    rec[nr_block].offset = buddy2_alloc(root, n);
    int i;
    for (i = 0; i < rec[nr_block].offset + 1; i++)
        le = list_next(le);
    page = le2page(le, page_link);
    int allocpages;
    if (!IS_POWER_OF_2(n))
        allocpages = fixsize(n);
    else {
        allocpages = n;
    }
    rec[nr_block].base = page;
    rec[nr_block].nr = allocpages;
    nr_block++;
    for (i = 0; i < allocpages; i++) {
        temp_le = list_next(le);
        p = le2page(le, page_link);
        ClearPageProperty(p);
        le = temp_le;
    }
    nr_free -= allocpages;
    page->property = n;
    return page;
}

int buddy2_alloc(struct buddy2* self, int size) {
  unsigned index = 0;
  unsigned node_size;
  unsigned offset = 0;

  if (self == NULL)
    return -1;

  if (size <= 0)
    size = 1;
  else if (!IS_POWER_OF_2(size))
    size = fixsize(size);

  if (self[index].longest < size)
    return -1;

  for (node_size = self->size; node_size != size; node_size /= 2) {
    if (self[LEFT_LEAF(index)].longest >= size) {
      if (self[RIGHT_LEAF(index)].longest >= size) {
        index = (self[LEFT_LEAF(index)].longest <= self[RIGHT_LEAF(index)].longest) ?
                LEFT_LEAF(index) : RIGHT_LEAF(index);
      } else {
        index = LEFT_LEAF(index);
      }
    } else {
      index = RIGHT_LEAF(index);
    }
  }

  self[index].longest = 0;
  offset = (index + 1) * node_size - self->size;
  while (index) {
    index = PARENT(index);
    self[index].longest = MAX(self[LEFT_LEAF(index)].longest, self[RIGHT_LEAF(index)].longest);
  }
  return offset;
}

void buddy_free_pages(struct Page* base, size_t n) {
  unsigned node_size, index = 0;
  unsigned left_longest, right_longest;
  struct buddy2* self = root;

  list_entry_t *le = list_next(&free_list);
  int i = 0;
  for (i = 0; i < nr_block; i++) {
    if (rec[i].base == base)
      break;
  }
  int offset = rec[i].offset;
  int pos = i;
  i = 0;
  while (i < offset) {
    le = list_next(le);
    i++;
  }
  int allocpages;
  if (!IS_POWER_OF_2(n))
    allocpages = fixsize(n);
  else {
    allocpages = n;
  }
  assert(self && offset >= 0 && offset < self->size);
  node_size = 1;
  index = offset + self->size - 1;
  nr_free += allocpages;
  struct Page* p;
  self[index].longest = allocpages;
  for (i = 0; i < allocpages; i++) {
    p = le2page(le, page_link);
    p->flags = 0;
    p->property = 1;
    SetPageProperty(p);
    le = list_next(le);
  }
  while (index) {
    index = PARENT(index);
    node_size *= 2;
    left_longest = self[LEFT_LEAF(index)].longest;
    right_longest = self[RIGHT_LEAF(index)].longest;
    if (left_longest + right_longest == node_size)
      self[index].longest = node_size;
    else
      self[index].longest = MAX(left_longest, right_longest);
  }
  for (i = pos; i < nr_block - 1; i++) {
    rec[i] = rec[i + 1];
  }
  nr_block--;
}

static size_t
buddy_nr_free_pages(void) {
    return nr_free;
}
//以下是一个测试函数
static void

buddy_check(void) {
    struct Page  *A, *B,*C,*D;
    A=alloc_pages(70);  
    B=alloc_pages(35);
    cprintf("A %p\n",A);
    cprintf("B %p\n",B);
    assert(A+128==B);
    C=alloc_pages(80);
    cprintf("C %p\n",C);
    assert(A+256==C);
    free_pages(A,70);
    cprintf("B %p\n",B);
    D=alloc_pages(60);
    cprintf("D %p\n",D);
    assert(B+64==D);
    free_pages(B,35);
    cprintf("D %p\n",D);
    free_pages(D,60);
    cprintf("C %p\n",C);
    free_pages(C,80);
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
