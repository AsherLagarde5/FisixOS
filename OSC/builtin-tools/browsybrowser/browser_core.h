#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Returns UTF-8 rendered text. The caller owns the result and must call browsy_free.
char* browsy_navigate(const char* url);
// Renders a UTF-8 HTML snippet without making a network request.
char* browsy_render_html(const char* html);
void browsy_free(char* value);

#ifdef __cplusplus
}
#endif
