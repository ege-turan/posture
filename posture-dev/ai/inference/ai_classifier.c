#include "ai_classifier.h"

#include <string.h>
#include <ctype.h>

#include "../generated/ai_vocab.h"
#include "../generated/ai_model_data.h"

#ifndef AI_MAX_TOKENS
#define AI_MAX_TOKENS 40
#endif

// FNV-1a 32-bit (must match training)
static uint32_t fnv1a_32(const char *s) {
    uint32_t h = 0x811c9dc5u;
    while (*s) {
        h ^= (uint8_t)(*s++);
        h *= 0x01000193u;
    }
    return h;
}

// Simple tokenizer: [a-z0-9'] sequences, lowercase, max token len limited
static int next_token(const char *s, int *idx, char *out, int out_sz) {
    int i = *idx;
    // skip non-token chars
    while (s[i] && !(isalnum((unsigned char)s[i]) || s[i] == '\'')) i++;
    if (!s[i]) { *idx = i; return 0; }

    int k = 0;
    while (s[i] && (isalnum((unsigned char)s[i]) || s[i] == '\'')) {
        if (k < out_sz - 1) out[k++] = (char)tolower((unsigned char)s[i]);
        i++;
    }
    out[k] = 0;
    *idx = i;
    return 1;
}

static int vocab_lookup_id(const char *tok) {
    uint32_t h = fnv1a_32(tok);
    for (int i = 1; i < AI_VOCAB_SIZE; i++) {
        if (ai_vocab_hashes[i] == h) {
            return i; // no strcmp check
        }
    }
    return 0;
}


ai_priority_t ai_classify_text(const char *text) {
    // Accumulate average embedding (int32)
    int32_t emb_acc[AI_EMB_DIM];
    for (int d = 0; d < AI_EMB_DIM; d++) emb_acc[d] = 0;

    int token_count = 0;

    int idx = 0;
    char tok[32];

    while (token_count < AI_MAX_TOKENS && next_token(text, &idx, tok, sizeof(tok))) {
        int tid = vocab_lookup_id(tok);
        const int8_t *emb = &ai_emb_q[tid * AI_EMB_DIM];
        for (int d = 0; d < AI_EMB_DIM; d++) {
            emb_acc[d] += (int32_t)emb[d];
        }
        token_count++;
    }

    if (token_count <= 0) token_count = 1;

    // Average in integer domain
    for (int d = 0; d < AI_EMB_DIM; d++) {
        emb_acc[d] = emb_acc[d] / token_count;
    }

    // Compute logits in int32:
    // logits_q[c] = bias_q[c] + sum_d (fc_q[c,d] * emb_avg_q[d])
    int32_t logits_q[AI_NUM_CLASSES];
    for (int c = 0; c < AI_NUM_CLASSES; c++) {
        int32_t acc = ai_fc_bias_q[c];
        const int8_t *w = &ai_fc_q[c * AI_EMB_DIM];
        for (int d = 0; d < AI_EMB_DIM; d++) {
            acc += (int32_t)w[d] * emb_acc[d];
        }
        logits_q[c] = acc;
    }

    // Argmax
    int best = 0;
    for (int c = 1; c < AI_NUM_CLASSES; c++) {
        if (logits_q[c] > logits_q[best]) best = c;
    }

    return (ai_priority_t)best;
}
