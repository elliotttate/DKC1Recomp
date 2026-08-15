#include "dkc1_script.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum Dkc1ScriptOpKind {
  kOpInput,      /* mask for count frames */
  kOpWait,       /* neutral/held mask until predicate or timeout */
  kOpPulse,      /* mask ON/OFF cycles until predicate or timeout */
  kOpCheckpoint,
  kOpStateSave,
  kOpStateLoad,
};

enum { kDkc1ScriptMaxSteps = 4096, kDkc1ScriptDefaultTimeout = 3600 };

typedef struct Dkc1ScriptStep {
  int kind;
  uint32_t input_mask;
  long count;           /* kOpInput frames, or wait/pulse timeout budget */
  long pulse_on;        /* kOpPulse: frames pressed per cycle */
  long pulse_off;       /* kOpPulse: frames released per cycle */
  uint32_t pulse_base;  /* kOpPulse: mask held through the off phase */
  uint32_t address;
  uint32_t value;
  uint32_t value_mask;
  uint8_t width;
  uint8_t shift;
  bool signed_compare;
  char op[3];
  char *text;           /* checkpoint name / state path */
  long line;
} Dkc1ScriptStep;

static Dkc1ScriptStep s_steps[kDkc1ScriptMaxSteps];
static size_t s_step_count;
static size_t s_cursor;
static long s_step_progress;
static bool s_active;
static bool s_failed;
static char s_error[256];

static char *Trim(char *text) {
  while (*text && isspace((unsigned char)*text)) text++;
  char *end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1])) *--end = '\0';
  return text;
}

static void SetError(const char *message, long line) {
  if (line > 0)
    snprintf(s_error, sizeof s_error, "line %ld: %s", line, message);
  else
    snprintf(s_error, sizeof s_error, "%s", message);
}

const char *Dkc1ScriptError(void) {
  return s_error;
}

void Dkc1ScriptStatus(char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) return;
  if (s_failed) {
    snprintf(buffer, buffer_size, "FAILED: %s", s_error);
    return;
  }
  if (!s_active || s_cursor >= s_step_count) {
    snprintf(buffer, buffer_size, "complete (%zu steps)", s_step_count);
    return;
  }
  const Dkc1ScriptStep *step = &s_steps[s_cursor];
  const char *kind = "unknown";
  switch (step->kind) {
    case kOpInput: kind = "input"; break;
    case kOpWait: kind = "wait"; break;
    case kOpPulse: kind = "pulse"; break;
    case kOpCheckpoint: kind = "checkpoint"; break;
    case kOpStateSave: kind = "state save"; break;
    case kOpStateLoad: kind = "state load"; break;
  }
  if (step->kind == kOpWait || step->kind == kOpPulse) {
    snprintf(buffer, buffer_size,
             "step %zu/%zu %s [%05X] %s %X (%ld/%ld)",
             s_cursor + 1, s_step_count, kind, step->address, step->op,
             step->value, s_step_progress, step->count);
  } else if (step->kind == kOpInput) {
    snprintf(buffer, buffer_size, "step %zu/%zu %s %03X (%ld/%ld)",
             s_cursor + 1, s_step_count, kind, step->input_mask,
             s_step_progress, step->count);
  } else {
    snprintf(buffer, buffer_size, "step %zu/%zu %s %s",
             s_cursor + 1, s_step_count, kind,
             step->text ? step->text : "");
  }
}

static bool ParseUnsigned(const char *token, unsigned long *value, int base) {
  if (!token || !*token || *token == '+' || *token == '-' ||
      isspace((unsigned char)*token))
    return false;
  char *end = NULL;
  unsigned long parsed = strtoul(token, &end, base);
  if (!end || end == token || *end != '\0')
    return false;
  *value = parsed;
  return true;
}

static uint32_t WidthMask(unsigned width) {
  return width == 4 ? UINT32_MAX : (UINT32_C(1) << (width * 8)) - 1u;
}

static bool ParsePredicate(char **tokens, int token_count,
                           Dkc1ScriptStep *step, long line) {
  /* ADDR OP VALUE [width 1|2|4] [mask M] [shift N] [signed]
   *               [timeout N] */
  unsigned long address, value;
  if (token_count < 3 || !ParseUnsigned(tokens[0], &address, 16) ||
      address > 0x1FFFFul) {
    SetError("expected hex WRAM address", line);
    return false;
  }
  const char *op = tokens[1];
  if (strcmp(op, "==") && strcmp(op, "!=") && strcmp(op, ">=") &&
      strcmp(op, "<=") && strcmp(op, ">") && strcmp(op, "<") &&
      strcmp(op, "&") && strcmp(op, "!&")) {
    SetError("operator must be == != > >= < <= & !&", line);
    return false;
  }
  if (!ParseUnsigned(tokens[2], &value, 16)) {
    SetError("expected hexadecimal comparison value", line);
    return false;
  }
  step->address = (uint32_t)address;
  step->value = (uint32_t)value;
  step->width = 2;
  step->shift = 0;
  step->signed_compare = false;
  bool mask_set = false;
  snprintf(step->op, sizeof step->op, "%s", op);
  step->count = kDkc1ScriptDefaultTimeout;
  for (int i = 3; i < token_count;) {
    if (strcmp(tokens[i], "signed") == 0) {
      step->signed_compare = true;
      i++;
      continue;
    }
    if (i + 1 >= token_count) {
      SetError("dangling predicate option", line);
      return false;
    }
    unsigned long extra;
    if (strcmp(tokens[i], "mask") == 0 &&
        ParseUnsigned(tokens[i + 1], &extra, 16)) {
      step->value_mask = (uint32_t)extra;
      mask_set = true;
    } else if (strcmp(tokens[i], "width") == 0 &&
               ParseUnsigned(tokens[i + 1], &extra, 10) &&
               (extra == 1 || extra == 2 || extra == 4)) {
      step->width = (uint8_t)extra;
    } else if (strcmp(tokens[i], "shift") == 0 &&
               ParseUnsigned(tokens[i + 1], &extra, 10) && extra < 32) {
      step->shift = (uint8_t)extra;
    } else if (strcmp(tokens[i], "timeout") == 0 &&
               ParseUnsigned(tokens[i + 1], &extra, 10) && extra >= 1 &&
               extra <= 1000000ul) {
      step->count = (long)extra;
    } else {
      SetError("options: width 1|2|4, mask HEX, shift N, signed, timeout N",
               line);
      return false;
    }
    i += 2;
  }
  const uint32_t width_mask = WidthMask(step->width);
  if (!mask_set) step->value_mask = width_mask;
  if (step->address + step->width > 0x20000u ||
      (step->value_mask & ~width_mask) != 0 ||
      step->shift >= step->width * 8 ||
      step->value > (step->value_mask >> step->shift) ||
      (step->signed_compare && step->value_mask == 0)) {
    SetError("predicate value/mask/shift exceeds its WRAM width", line);
    return false;
  }
  return true;
}

static bool AppendStep(const Dkc1ScriptStep *step, long line) {
  if (s_step_count >= kDkc1ScriptMaxSteps) {
    SetError("script has too many steps", line);
    return false;
  }
  s_steps[s_step_count++] = *step;
  return true;
}

static bool SafeCheckpointName(const char *text) {
  if (!text || !*text || strlen(text) > 64 ||
      !(isalnum((unsigned char)text[0]) || text[0] == '_'))
    return false;
  for (const char *p = text + 1; *p; p++)
    if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-' ||
          *p == '.'))
      return false;
  return true;
}

bool Dkc1ScriptLoad(const char *path, char *error, size_t error_size) {
  Dkc1ScriptFree();
  s_error[0] = '\0';
  FILE *stream = fopen(path, "r");
  if (!stream) {
    SetError("unable to open script", 0);
    if (error) snprintf(error, error_size, "%s", s_error);
    return false;
  }

  char line_text[512];
  long line = 0;
  bool ok = true;
  while (ok && fgets(line_text, sizeof line_text, stream)) {
    line++;
    char *comment = strpbrk(line_text, "#;");
    if (comment) *comment = '\0';
    char *text = Trim(line_text);
    if (!*text) continue;

    char *tokens[20];
    int token_count = 0;
    for (char *cursor = strtok(text, " \t"); cursor && token_count < 20;
         cursor = strtok(NULL, " \t"))
      tokens[token_count++] = cursor;
    if (!token_count) continue;

    Dkc1ScriptStep step;
    memset(&step, 0, sizeof step);
    step.line = line;

    if (strcmp(tokens[0], "pulse") == 0) {
      /* pulse MASK ON OFF ADDR OP VALUE [options] — press MASK for ON
       * frames then release for OFF frames, repeating until the predicate
       * passes. Menu traversal needs edge-triggered presses, not holds. */
      unsigned long mask, on, off;
      if (token_count < 7 || !ParseUnsigned(tokens[1], &mask, 16) ||
          !ParseUnsigned(tokens[2], &on, 10) || on < 1 || on > 600 ||
          !ParseUnsigned(tokens[3], &off, 10) || off < 1 || off > 600) {
        SetError("pulse needs MASK ON OFF then a predicate", line);
        ok = false;
      } else {
        step.kind = kOpPulse;
        step.input_mask = (uint32_t)mask;
        step.pulse_on = (long)on;
        step.pulse_off = (long)off;
        int predicate_at = 4;
        unsigned long base = 0;
        if (token_count > 6 && strcmp(tokens[4], "base") == 0 &&
            ParseUnsigned(tokens[5], &base, 16)) {
          step.pulse_base = (uint32_t)base;
          step.input_mask |= step.pulse_base;
          predicate_at = 6;
        }
        ok = ParsePredicate(tokens + predicate_at,
                            token_count - predicate_at, &step, line) &&
             AppendStep(&step, line);
      }
    } else if (strcmp(tokens[0], "wait") == 0) {
      step.kind = kOpWait;
      step.input_mask = 0;
      ok = ParsePredicate(tokens + 1, token_count - 1, &step, line) &&
           AppendStep(&step, line);
    } else if (strcmp(tokens[0], "hold") == 0) {
      unsigned long mask;
      if (token_count < 5 || !ParseUnsigned(tokens[1], &mask, 16)) {
        SetError("hold needs MASK then a predicate", line);
        ok = false;
      } else {
        step.kind = kOpWait;
        step.input_mask = (uint32_t)mask;
        ok = ParsePredicate(tokens + 2, token_count - 2, &step, line) &&
             AppendStep(&step, line);
      }
    } else if (strcmp(tokens[0], "checkpoint") == 0 ||
               strcmp(tokens[0], "state_save") == 0 ||
               strcmp(tokens[0], "state_load") == 0) {
      if (token_count != 2) {
        SetError("directive needs exactly one argument", line);
        ok = false;
      } else if (strcmp(tokens[0], "checkpoint") == 0 &&
                 !SafeCheckpointName(tokens[1])) {
        SetError("checkpoint name must be a safe 1..64 character filename",
                 line);
        ok = false;
      } else {
        step.kind = strcmp(tokens[0], "checkpoint") == 0 ? kOpCheckpoint
                    : strcmp(tokens[0], "state_save") == 0 ? kOpStateSave
                                                           : kOpStateLoad;
        step.text = _strdup(tokens[1]);
        ok = step.text && AppendStep(&step, line);
      }
    } else {
      /* MASK [* COUNT] — input_playback compatibility */
      unsigned long mask;
      if (!ParseUnsigned(tokens[0], &mask, 16) || mask > 0xFFFFFFul) {
        SetError("expected hex input mask or directive", line);
        ok = false;
      } else {
        unsigned long repeat = 1;
        int index = 1;
        if (index < token_count && strcmp(tokens[index], "*") == 0) index++;
        if (index < token_count &&
            (!ParseUnsigned(tokens[index], &repeat, 10) || repeat < 1 ||
             repeat > 1000000ul)) {
          SetError("repeat count must be 1..1000000", line);
          ok = false;
        }
        if (ok) {
          step.kind = kOpInput;
          step.input_mask = (uint32_t)mask;
          step.count = (long)repeat;
          ok = AppendStep(&step, line);
        }
      }
    }
  }
  const bool read_error = ferror(stream) != 0;
  fclose(stream);
  if (read_error) {
    SetError("unable to read script", 0);
    ok = false;
  }
  if (!ok) {
    if (error) snprintf(error, error_size, "%s", s_error);
    Dkc1ScriptFree();
    return false;
  }
  s_active = s_step_count > 0;
  return true;
}

bool Dkc1ScriptActive(void) {
  return s_active;
}

bool Dkc1ScriptFinished(void) {
  return !s_active || s_cursor >= s_step_count;
}

static bool PredicatePasses(const Dkc1ScriptStep *step, const uint8_t *wram) {
  uint32_t raw = 0;
  for (unsigned i = 0; i < step->width; i++)
    raw |= (uint32_t)wram[step->address + i] << (i * 8);
  const uint32_t value = (raw & step->value_mask) >> step->shift;
  if (strcmp(step->op, "==") == 0) return value == step->value;
  if (strcmp(step->op, "!=") == 0) return value != step->value;
  if (strcmp(step->op, "&") == 0) return (value & step->value) != 0;
  if (strcmp(step->op, "!&") == 0) return (value & step->value) == 0;

  int64_t left = value;
  int64_t right = step->value;
  if (step->signed_compare) {
    uint32_t normalized_mask = step->value_mask >> step->shift;
    uint32_t sign_bit = 1;
    while (sign_bit <= UINT32_MAX / 2 &&
           (sign_bit << 1) <= normalized_mask)
      sign_bit <<= 1;
    if (value & sign_bit) left -= (int64_t)sign_bit << 1;
    if (step->value & sign_bit) right -= (int64_t)sign_bit << 1;
  }
  if (strcmp(step->op, ">=") == 0) return left >= right;
  if (strcmp(step->op, "<=") == 0) return left <= right;
  if (strcmp(step->op, ">") == 0) return left > right;
  return left < right;
}

uint32_t Dkc1ScriptNextInput(const uint8_t *wram, Dkc1ScriptOps *ops,
                             bool *failed) {
  if (ops) memset(ops, 0, sizeof *ops);
  if (failed) *failed = false;
  if (!s_active || s_failed)
    return 0;

  /* Zero-frame directives execute immediately, attached to this frame. */
  while (s_cursor < s_step_count) {
    Dkc1ScriptStep *step = &s_steps[s_cursor];
    if (step->kind == kOpCheckpoint) {
      if (ops) ops->checkpoint = step->text;
      s_cursor++;
      return 0;
    } else if (step->kind == kOpStateSave) {
      if (ops) ops->state_save = step->text;
      s_cursor++;
      return 0;
    } else if (step->kind == kOpStateLoad) {
      if (ops) ops->state_load = step->text;
      s_cursor++;
      /* Loading replaces WRAM. Return to the host immediately so the next
       * predicate is evaluated against the restored state, never the state
       * that preceded this boundary. */
      return 0;
    } else {
      break;
    }
  }
  if (s_cursor >= s_step_count)
    return 0;

  Dkc1ScriptStep *step = &s_steps[s_cursor];
  if (step->kind == kOpInput) {
    if (ops) ops->run_frame = true;
    uint32_t mask = step->input_mask;
    if (++s_step_progress >= step->count) {
      s_cursor++;
      s_step_progress = 0;
    }
    return mask;
  }

  /* kOpWait */
  if (PredicatePasses(step, wram)) {
    s_cursor++;
    s_step_progress = 0;
    /* Re-evaluate from the new step this same frame so a satisfied wait
     * does not consume an extra neutral frame. */
    return Dkc1ScriptNextInput(wram, ops, failed);
  }
  if (s_step_progress >= step->count) {
    s_failed = true;
    char message[128];
    snprintf(message, sizeof message,
             "wait timed out: [%05X]&%08X >> %u %s %08X after %ld frames",
             step->address, step->value_mask, step->shift, step->op,
             step->value, step->count);
    SetError(message, step->line);
    if (failed) *failed = true;
    return 0;
  }
  s_step_progress++;
  if (ops) ops->run_frame = true;
  if (step->kind == kOpPulse) {
    const long cycle = step->pulse_on + step->pulse_off;
    return (s_step_progress - 1) % cycle < step->pulse_on
               ? step->input_mask : step->pulse_base;
  }
  return step->input_mask;
}

void Dkc1ScriptFree(void) {
  for (size_t i = 0; i < s_step_count; i++)
    free(s_steps[i].text);
  memset(s_steps, 0, sizeof s_steps);
  s_step_count = 0;
  s_cursor = 0;
  s_step_progress = 0;
  s_active = false;
  s_failed = false;
}
