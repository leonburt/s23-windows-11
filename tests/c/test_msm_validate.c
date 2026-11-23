/* drivers/s23gpu/msm_validate.h: escape size table, user pointer ranges, submit / submit_bo / submit_cmd
 * bounds. The header is included unmodified. */
#include <string.h>
#include "host_test.h"
#include "msm_validate.h"

#define LIMIT 0x00007FFFFFFF0000ULL   /* MM_USER_PROBE_ADDRESS (64-bit Windows) */

static struct topazgpu_escape esc(unsigned nr, unsigned size)
{
    struct topazgpu_escape e;
    memset(&e, 0, sizeof(e));
    e.magic = TOPAZGPU_ESC_MAGIC;
    e.nr = nr;
    e.size = size;
    return e;
}

static void test_escape(void)
{
    static const struct { unsigned nr; unsigned need; } ops[] = {
        { TOPAZGPU_NR_VERSION, sizeof(struct topazgpu_version) },
        { TOPAZGPU_NR_MMAP, sizeof(struct topazgpu_mmap) },
        { TOPAZGPU_NR_MUNMAP, sizeof(struct topazgpu_mmap) },
        { TOPAZGPU_NR_GEM_CLOSE, sizeof(struct drm_gem_close) },
        { TOPAZGPU_NR_GEM_OPEN, sizeof(struct drm_gem_open) },
        { DRM_COMMAND_BASE + DRM_MSM_GEM_NEW, sizeof(struct drm_msm_gem_new) },
        { DRM_COMMAND_BASE + DRM_MSM_GEM_SUBMIT, sizeof(struct drm_msm_gem_submit) },
        { DRM_COMMAND_BASE + DRM_MSM_WAIT_FENCE, sizeof(struct drm_msm_wait_fence) },
        { DRM_COMMAND_BASE + DRM_MSM_SUBMITQUEUE_CLOSE, sizeof(__u32) },
    };
    struct topazgpu_escape e;

    CHECK(MsmValidateEscape(NULL) == -22);
    e = esc(TOPAZGPU_NR_VERSION, 64); e.magic = 0;   CHECK(MsmValidateEscape(&e) == -22);
    e = esc(TOPAZGPU_NR_VERSION, TOPAZGPU_ESC_MAX_DATA + 1); CHECK(MsmValidateEscape(&e) == -22);
    e = esc(0xDEAD, 64);                              CHECK(MsmValidateEscape(&e) == -38);
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        CHECK(ops[i].need <= TOPAZGPU_ESC_MAX_DATA);  /* every payload fits the fixed data[] */
        e = esc(ops[i].nr, ops[i].need);     CHECK(MsmValidateEscape(&e) == 0);
        e = esc(ops[i].nr, ops[i].need - 1); CHECK(MsmValidateEscape(&e) == -22);
        e = esc(ops[i].nr, TOPAZGPU_ESC_MAX_DATA); CHECK(MsmValidateEscape(&e) == 0);
    }
}

static void test_user_range(void)
{
    CHECK(MsmUserRange(0x1000, 8, LIMIT));
    CHECK(!MsmUserRange(0, 8, LIMIT));                       /* NULL */
    CHECK(!MsmUserRange(0x1000, 0, LIMIT));                  /* empty */
    CHECK(!MsmUserRange(0x1004, 8, LIMIT));                  /* unaligned */
    CHECK(!MsmUserRange(LIMIT, 8, LIMIT));                   /* at the limit */
    CHECK(MsmUserRange(LIMIT - 8, 8, LIMIT));                /* ends exactly at the limit */
    CHECK(!MsmUserRange(LIMIT - 8, 16, LIMIT));              /* crosses the limit */
    CHECK(!MsmUserRange(0x1000, 0xFFFFFFFFFFFFF000ULL, LIMIT)); /* address + bytes wraps */
}

static struct drm_msm_gem_submit good_submit(void)
{
    struct drm_msm_gem_submit s;
    memset(&s, 0, sizeof(s));
    s.flags = MSM_PIPE_3D0;
    s.nr_bos = 2;
    s.nr_cmds = 1;
    s.bos = 0x10000;
    s.cmds = 0x20000;
    return s;
}

static void test_submit(void)
{
    struct drm_msm_gem_submit s;
    __u64 bb = 0, cb = 0;

    s = good_submit();
    CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == 0);
    CHECK_EQ_U64(bb, 2 * sizeof(struct drm_msm_gem_submit_bo));
    CHECK_EQ_U64(cb, sizeof(struct drm_msm_gem_submit_cmd));

    s = good_submit(); s.nr_bos = 0;                          CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -22);
    s = good_submit(); s.nr_bos = S23GPU_SUBMIT_MAX_BOS + 1;  CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -22);
    s = good_submit(); s.nr_bos = S23GPU_SUBMIT_MAX_BOS;      CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == 0);
    s = good_submit(); s.nr_cmds = 0;                         CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -22);
    s = good_submit(); s.nr_cmds = S23GPU_SUBMIT_MAX_CMDS + 1; CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -22);
    s = good_submit(); s.pad = 1;                             CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -22);
    s = good_submit(); s.flags = 0x11;                        CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -38);
    s = good_submit(); s.flags |= MSM_SUBMIT_NO_IMPLICIT;     CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == 0);
    s = good_submit(); s.flags |= 0x40000000u;                CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -38);
    s = good_submit(); s.queueid = 2;                         CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -38);
    s = good_submit(); s.nr_in_syncobjs = 1;                  CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -38);
    s = good_submit(); s.bos = 0x10004;                       CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -14);
    s = good_submit(); s.cmds = LIMIT - 8;                    CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -14);
    s = good_submit(); s.bos = 0xFFFF800000000000ULL;         CHECK(MsmValidateSubmit(&s, LIMIT, &bb, &cb) == -14);
}

static void test_submit_bo(void)
{
    struct drm_msm_gem_submit_bo b;
    memset(&b, 0, sizeof(b));
    b.handle = 1; b.flags = MSM_SUBMIT_BO_READ;                     CHECK(MsmValidateSubmitBo(&b) == 0);
    b.flags = MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE | MSM_SUBMIT_BO_DUMP; CHECK(MsmValidateSubmitBo(&b) == 0);
    b.flags = 0;                                                    CHECK(MsmValidateSubmitBo(&b) == -22);
    b.flags = MSM_SUBMIT_BO_DUMP;                                   CHECK(MsmValidateSubmitBo(&b) == -22);
    b.flags = MSM_SUBMIT_BO_READ | 0x80000000u;                     CHECK(MsmValidateSubmitBo(&b) == -22);
    b.flags = MSM_SUBMIT_BO_READ; b.handle = 0;                     CHECK(MsmValidateSubmitBo(&b) == -22);
}

static struct drm_msm_gem_submit_cmd cmd(__u32 off, __u32 size)
{
    struct drm_msm_gem_submit_cmd c;
    memset(&c, 0, sizeof(c));
    c.type = MSM_SUBMIT_CMD_BUF;
    c.submit_offset = off;
    c.size = size;
    return c;
}

static void test_submit_cmd(void)
{
    struct drm_msm_gem_submit_cmd c;
    const __u64 bo = 0x10000, iova = 0x02000000ULL;

    c = cmd(0, 0x100);         CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == 0);
    c = cmd(0xFF00, 0x100);    CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == 0);      /* ends at BO end */
    c = cmd(0xFF00, 0x104);    CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == -22);    /* past the end */
    c = cmd(0x10004, 4);       CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == -22);    /* offset > size */
    c = cmd(4, 0xFFFFFFFCu);   CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == -22);    /* off + size wraps */
    c = cmd(0, 0);             CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == -22);
    c = cmd(2, 4);             CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == -22);    /* unaligned offset */
    c = cmd(0, 6);             CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == -22);    /* unaligned size */
    c = cmd(0, 4);             CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova + 2) == -22);
    c = cmd(0, 4);             CHECK(MsmValidateSubmitCmd(&c, 1, bo, 0xFFFFFFFFFFFF0000ULL) == -22); /* iova + size wraps */
    c = cmd(0, 4); c.submit_idx = 1; CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == -22);
    c = cmd(0, 4); c.nr_relocs = 1;  CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == -38);
    c = cmd(0, 4); c.type = 99;      CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == -38);
    c = cmd(0, 4); c.pad = 1;        CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == -22);
    c = cmd(0, 4); c.type = MSM_SUBMIT_CMD_CTX_RESTORE_BUF; CHECK(MsmValidateSubmitCmd(&c, 1, bo, iova) == 0);
}

int main(void)
{
    test_escape();
    test_user_range();
    test_submit();
    test_submit_bo();
    test_submit_cmd();
    return test_report("test_msm_validate");
}
