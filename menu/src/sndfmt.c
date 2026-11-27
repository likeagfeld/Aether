/* menu/src/sndfmt.c -- see sndfmt.h. BSD-3-Clause. */
#include "om_fmt.h"
#include "sndfmt.h"

int om_sndfile_open(om_sndfile *f, const uint8_t *buf, uint32_t buf_len, uint32_t file_size)
{
    om_header h;
    int rc = om_header_read(buf, buf_len, file_size, "OMSATSND", &h);
    if (rc)
        return rc;
    if (h.count < 1 || h.count > OM_SND_MAXCLIP || h.table_off != OM_HEADER_SIZE || h.table_size != 24u * h.count ||
        h.data_off % OM_SECTOR)
        return OM_E_FIELD;
    f->buf = buf;
    f->count = h.count;
    f->data_off = h.data_off;
    f->data_size = h.data_size;
    return OM_OK;
}

int om_sndfile_clip(const om_sndfile *f, uint32_t i, om_snd_clip *c)
{
    const uint8_t *r;
    if (i >= f->count)
        return OM_E_RANGE;
    r = f->buf + OM_HEADER_SIZE + 24u * i;
    c->off = om_be32(r);
    c->len = om_be32(r + 4);
    c->samples = om_be32(r + 8);
    c->rate = om_be16(r + 12);
    c->oct = (int8_t)r[14];
    c->flags = r[15];
    c->fns = om_be16(r + 16);
    c->id = r[18];
    if (c->off % OM_SECTOR || c->len > f->data_size || c->off > f->data_size - c->len)
        return OM_E_FIELD;
    if (!c->samples || c->samples > OM_SND_MAXSAMP || c->len != c->samples * ((c->flags & OM_SND_F16) ? 2u : 1u))
        return OM_E_FIELD;
    if (c->fns > 1023u || c->oct < -8 || c->oct > 7)
        return OM_E_FIELD;
    return OM_OK;
}

int om_mus_open(om_mus *m, const uint8_t *buf, uint32_t buf_len, uint32_t file_size)
{
    om_header h;
    const uint8_t *r;
    int rc = om_header_read(buf, buf_len, file_size, "OMSATMUS", &h);
    if (rc)
        return rc;
    if (h.table_off != OM_HEADER_SIZE || h.table_size != 32u || h.data_off != OM_MUS_HEAD || !h.count ||
        h.data_size != h.count * OM_MUS_BLOCK_BYTES || h.count > (0x7FFFFFFFu - OM_MUS_HEAD) / OM_MUS_BLOCK_BYTES || /* s_seek: int */
        h.data_off + h.data_size > file_size)
        return OM_E_FIELD;
    r = buf + OM_HEADER_SIZE;
    m->blocks = h.count;
    m->data_off = h.data_off;
    m->rate = om_be32(r);
    if (om_be16(r + 4) != 2u || om_be16(r + 6) != 16u || om_be32(r + 8) != OM_MUS_BLOCK)
        return OM_E_FIELD;                           /* the menu plays 2 ch, 16-bit, 4096-frame blocks only */
    m->frames = om_be32(r + 12);
    m->start_frame = om_be32(r + 16);
    m->gain_cdb = (int16_t)om_be16(r + 20);
    m->oct = (int8_t)r[22];
    m->fns = om_be16(r + 24);
    if (!m->frames || m->frames > h.count * OM_MUS_BLOCK || m->fns > 1023u || m->oct < -8 || m->oct > 7)
        return OM_E_FIELD;
    if (m->start_frame >= m->frames)
        m->start_frame = 0;
    return OM_OK;
}

int om_mus_retry(uint8_t *tries, int rc)
{
    if (rc != -120 && !*tries) return OM_MUS_SHOW;              /* menu/app/sd.h OM_SD_TIMEOUT (test_sndfmt checks it) */
    if (*tries >= OM_MUS_RETRIES) return OM_MUS_QUIET;
    ++*tries;
    return OM_MUS_RETRY;
}

int om_mus_recover(uint32_t ok_at_fail, uint32_t ok_now, uint32_t vb_since_fail, uint32_t min_vb)
{
    return ok_now != ok_at_fail && vb_since_fail >= min_vb;
}
