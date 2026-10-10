#include <stdio.h>
#include <string.h>
#include <mpv/client.h>

static void dump(mpv_node *n, int depth);

int main(void)
{
  mpv_handle *h = mpv_create();
  mpv_set_option_string(h, "config", "no");
  mpv_set_option_string(h, "vo", "null");
  mpv_set_option_string(h, "ao", "null");
  mpv_initialize(h);
  printf("mpv client api %lu, version %s\n", (unsigned long)mpv_client_api_version(),
         mpv_get_property_string(h, "mpv-version"));
  mpv_node list;
  if (mpv_get_property(h, "command-list", MPV_FORMAT_NODE, &list) < 0)
    { printf("no command-list\n"); return 1; }
  for (int i = 0; i < list.u.list->num; i++)
  {
    mpv_node *cmd = &list.u.list->values[i];
    const char *name = NULL;
    for (int k = 0; k < cmd->u.list->num; k++)
      if (!strcmp(cmd->u.list->keys[k], "name"))
        name = cmd->u.list->values[k].u.string;
    if (!name || (strcmp(name, "loadfile") && strcmp(name, "sub-add"))) continue;
    printf("=== %s\n", name);
    dump(cmd, 1);
  }
  return 0;
}

static void dump(mpv_node *n, int depth)
{
  char pad[32]; memset(pad, ' ', sizeof pad); pad[depth * 2 > 30 ? 30 : depth * 2] = 0;
  switch (n->format)
  {
    case MPV_FORMAT_NODE_MAP:
      for (int i = 0; i < n->u.list->num; i++)
        { printf("%s%s:\n", pad, n->u.list->keys[i]); dump(&n->u.list->values[i], depth + 1); }
      break;
    case MPV_FORMAT_NODE_ARRAY:
      for (int i = 0; i < n->u.list->num; i++)
        { printf("%s- [%d]\n", pad, i); dump(&n->u.list->values[i], depth + 1); }
      break;
    case MPV_FORMAT_STRING: printf("%s%s\n", pad, n->u.string); break;
    case MPV_FORMAT_FLAG:   printf("%s%s\n", pad, n->u.flag ? "true" : "false"); break;
    case MPV_FORMAT_INT64:  printf("%s%lld\n", pad, (long long)n->u.int64); break;
    default:                printf("%s<fmt %d>\n", pad, n->format); break;
  }
}
