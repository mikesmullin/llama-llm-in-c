#include <llama.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.c"
#include "regex.h"  // tiny-regex-c

// static const char* MODEL_PATH = "vendor/models/SmolLM2-1.7B-Instruct-Q4_K_M.gguf";
static const char* MODEL_PATH = "vendor/models/Meta-Llama-3.1-8B-Instruct-Q5_K_M.gguf";

#define MAX_RESPONSE_SIZE (1024 * 10)
#define MAX_PROMPT_SIZE (1024 * 10)
#define MAX_USER_INPUT (1024 * 10)
#define INITIAL_MSG_CAPACITY 8
#define CMD_SZ (1024 * 10)
#define CTX_SZ (1024 * 10)
#define PROMPT_SZ (1024 * 10)

// Logging callback for error output only
void log_callback(enum ggml_log_level level, const char* text, void* user_data) {
  if (level >= GGML_LOG_LEVEL_ERROR) {
    fprintf(stderr, "%s", text);
  }
}

// Tokenizes a prompt string into an array of llama_token
llama_token* tokenize_prompt(
    const char* prompt, const struct llama_vocab* vocab, int* out_count, int is_first) {
  int token_count = -llama_tokenize(vocab, prompt, strlen(prompt), NULL, 0, is_first, 1);
  llama_token* tokens = (llama_token*)malloc(sizeof(llama_token) * token_count);
  if (llama_tokenize(vocab, prompt, strlen(prompt), tokens, token_count, is_first, 1) < 0) {
    GGML_ABORT("Failed to tokenize the prompt\n");
  }
  *out_count = token_count;
  return tokens;
}

// Generates text from the given prompt using llama
char* generate_response(
    const char* prompt,
    struct llama_context* ctx,
    struct llama_sampler* sampler,
    const struct llama_vocab* vocab) {
  static char response[MAX_RESPONSE_SIZE];
  response[0] = '\0';
  int is_first = llama_memory_seq_pos_max(llama_get_memory(ctx), 0) == -1;

  int token_count;
  llama_token* tokens = tokenize_prompt(prompt, vocab, &token_count, is_first);
  llama_batch batch = llama_batch_get_one(tokens, token_count);

  int response_len = 0;
  while (1) {
    int n_ctx = llama_n_ctx(ctx);
    int n_ctx_used = llama_memory_seq_pos_max(llama_get_memory(ctx), 0) + 1;

    if (n_ctx_used + batch.n_tokens > n_ctx) {
      printf("\033[0m\n");
      fprintf(stderr, "Context size exceeded\n");
      exit(1);
    }

    if (llama_decode(ctx, batch) != 0) {
      GGML_ABORT("Failed to decode\n");
    }

    llama_token token = llama_sampler_sample(sampler, ctx, -1);
    if (llama_vocab_is_eog(vocab, token)) {
      break;
    }

    char buf[256];
    int n = llama_token_to_piece(vocab, token, buf, sizeof(buf), 0, 1);
    if (n < 0) {
      GGML_ABORT("Token to piece conversion failed\n");
    }
    buf[n] = '\0';

    printf("%s", buf);
    fflush(stdout);

    if (response_len + n < MAX_RESPONSE_SIZE - 1) {
      memcpy(response + response_len, buf, n);
      response_len += n;
      response[response_len] = '\0';
    }

    batch = llama_batch_get_one(&token, 1);
  }

  free(tokens);
  return response;
}

// Applies the chat template and resizes buffer if needed
int apply_chat_template(
    const char* tmpl,
    llama_chat_message* messages,
    int msg_count,
    int partial,
    char** out_buf,
    int* buf_size) {
  int len = llama_chat_apply_template(tmpl, messages, msg_count, partial, *out_buf, *buf_size);
  if (len > *buf_size) {
    *out_buf = (char*)realloc(*out_buf, len);
    *buf_size = len;
    len = llama_chat_apply_template(tmpl, messages, msg_count, partial, *out_buf, *buf_size);
  }
  return len;
}

// Adds a chat message to the dynamic message array
void add_message(
    llama_chat_message** msgs, int* count, int* capacity, const char* role, const char* content) {
  if (*count == *capacity) {
    *capacity = *capacity ? *capacity * 2 : INITIAL_MSG_CAPACITY;
    *msgs = (llama_chat_message*)realloc(*msgs, sizeof(llama_chat_message) * (*capacity));
  }
  (*msgs)[*count].role = role;
  (*msgs)[*count].content = _strdup(content);
  (*count)++;
}

int main(void) {
  // Set console to UTF-8 mode
  SetConsoleOutputCP(CP_UTF8);

  llama_log_set(log_callback, NULL);
  ggml_backend_load_all();

  struct llama_model_params model_params = llama_model_default_params();
  model_params.n_gpu_layers = 99;

  printf("Loading %s\n", MODEL_PATH);
  struct llama_model* model = llama_model_load_from_file(MODEL_PATH, model_params);
  if (!model) {
    return fprintf(stderr, "Failed to load model\n"), 1;
  }

  const struct llama_vocab* vocab = llama_model_get_vocab(model);
  struct llama_context_params ctx_params = llama_context_default_params();
  ctx_params.n_ctx = CTX_SZ;
  ctx_params.n_batch = CTX_SZ;
  struct llama_context* ctx = llama_init_from_model(model, ctx_params);
  if (!ctx) {
    return fprintf(stderr, "Failed to initialize context\n"), 1;
  }

  struct llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
  llama_sampler_chain_add(sampler, llama_sampler_init_min_p(0.05f, 1));
  llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.8f));
  llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

  llama_chat_message* messages = NULL;
  int msg_count = 0, msg_capacity = 0;
  char* formatted = (char*)malloc(ctx_params.n_ctx);
  int formatted_capacity = ctx_params.n_ctx, prev_len = 0;

  const char* chat_template = llama_model_chat_template(model, NULL);
  // Read initial prompt from prompts/system.md
  FILE* f = fopen("prompts/system.md", "rb");
  if (!f) {
    return fprintf(stderr, "Failed to open prompts/system.md\n"), 1;
  }
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* system_prompt = (char*)malloc(fsize + 1);
  if (!system_prompt) {
    fclose(f);
    return fprintf(stderr, "Failed to allocate memory for INIT_PROMPT\n"), 1;
  }
  fread(system_prompt, 1, fsize, f);
  system_prompt[fsize] = '\0';
  fclose(f);

  add_message(&messages, &msg_count, &msg_capacity, "user", system_prompt);
  int new_len =
      apply_chat_template(chat_template, messages, msg_count, 1, &formatted, &formatted_capacity);
  if (new_len < 0) {
    return fprintf(stderr, "Template application failed\n"), 1;
  }

  char prompt[MAX_PROMPT_SIZE];
  int prompt_len = (new_len > MAX_PROMPT_SIZE - 1) ? MAX_PROMPT_SIZE - 1 : new_len;
  memcpy(prompt, formatted, prompt_len);
  prompt[prompt_len] = '\0';

  printf("\033[33m");
  char* response = generate_response(prompt, ctx, sampler, vocab);
  printf("\n\033[0m");

  add_message(&messages, &msg_count, &msg_capacity, "assistant", response);
  prev_len = llama_chat_apply_template(chat_template, messages, msg_count, 0, NULL, 0);
  if (prev_len < 0) {
    return fprintf(stderr, "Template application failed\n"), 1;
  }

  char user_input[MAX_USER_INPUT];
  int input_len = 0;
  bool keep_input = false;
  while (1) {
    printf("\033[32m> \033[0m");
    if (!keep_input) {
      // printf("not keeping input\n");
      user_input[0] = '\0';
      input_len = 0;
    }
    int c;
    // detect Ctrl+Z (ASCII 26) as end of input.
    while ((c = getchar()) != EOF && c != 26 && input_len < MAX_USER_INPUT - 1) {
      user_input[input_len++] = (char)c;
    }
    user_input[input_len] = '\0';
    if ((c == EOF || c == 26) && input_len == 0) {
      break;
    }

    add_message(&messages, &msg_count, &msg_capacity, "user", user_input);
    new_len =
        apply_chat_template(chat_template, messages, msg_count, 1, &formatted, &formatted_capacity);
    if (new_len < 0) {
      return fprintf(stderr, "Template application failed\n"), 1;
    }

    int prompt_len = new_len - prev_len;
    if (prompt_len > MAX_PROMPT_SIZE - 1) {
      prompt_len = MAX_PROMPT_SIZE - 1;
    }
    memcpy(prompt, formatted + prev_len, prompt_len);
    prompt[prompt_len] = '\0';

    // printf("prompt %s", prompt);
    printf("\033[33m");
    char* response = generate_response(prompt, ctx, sampler, vocab);
    printf("\n\033[0m");

    // Check if the response contains a command request using regex
    keep_input = false;
    char command[CMD_SZ] = {0};
    int coffset = 0, roffset = 0, rlen = 0;
    while (-1 !=
           (rlen = re_match_group("```\\s*e?x?e?c?\\s*", "\\s*```", response + roffset, command))) {
      roffset += rlen;
      char out[CMD_SZ] = {0}, err[CMD_SZ] = {0}, res[CMD_SZ] = {0};
      int exec_code = -1;
      exec_code = prompt_exec(command, out, err, res);
      if (-1 != exec_code) {
        printf("%s", res);

        if (ask("🤖 Share result with LLM?")) {
          input_len = strlen(res);
          memcpy(user_input + coffset, res, input_len);
          user_input[coffset + input_len] = 0;
          coffset += input_len;
          // add_message(&messages, &msg_count, &msg_capacity, "user", res);
          // prev_len = llama_chat_apply_template(chat_template, messages, msg_count, 0, NULL, 0);
          keep_input = true;
        }
      }
    }

    add_message(&messages, &msg_count, &msg_capacity, "assistant", response);
    prev_len = llama_chat_apply_template(chat_template, messages, msg_count, 0, NULL, 0);
    // prev_len = llama_chat_apply_template(chat_template, messages, msg_count, 0, NULL, 0);
    if (prev_len < 0) {
      return fprintf(stderr, "Template application failed\n"), 1;
    }
  }

  free(system_prompt);
  for (int i = 0; i < msg_count; ++i) {
    free((char*)messages[i].content);
  }
  free(messages);
  free(formatted);
  llama_sampler_free(sampler);
  llama_free(ctx);
  llama_model_free(model);
  return 0;
}
