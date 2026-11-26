#include "focus.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(const char *display, const char *expected) {
    char *package = mb_focused_package(display);
    assert(!strcmp(package ? package : "", expected));
    free(package);
}

int main(void) {
    const char *settings = "  mCurrentFocus=Window{b825493 u0 com.android.settings/com.android.settings.homepage.SettingsHomepageActivity}\n";
    check(settings, "com.android.settings");
    char text[1024];
    snprintf(text, sizeof(text), "  mCurrentFocus=null\n%s", settings);
    check(text, "com.android.settings");
    snprintf(text, sizeof(text), "%s  mCurrentFocus=null\n", settings);
    check(text, "com.android.settings");
    check("mCurrentFocus=null\n", "");
    check("mFocusedApp=ActivityRecord{123 u0 com.android.settings/.Settings}\n", "");
    check("mCurrentFocus=Window{a u0 com.example.game/.Game}\n", "com.example.game");
    check("mCurrentFocus=Window{a u0 com.google.android.apps.nexuslauncher/.NexusLauncherActivity}\n",
          "com.google.android.apps.nexuslauncher");
    snprintf(text, sizeof(text), "%smCurrentFocus=Window{a u0 com.example.game/.Game}\n", settings);
    check(text, "");
    snprintf(text, sizeof(text), "%s%s", settings, settings);
    check(text, "com.android.settings");
    check("mCurrentFocus=Window{invalid}\n", "");
    check("", "");
    check(NULL, "");
    puts("PASS: live display focus, null focus, absent focus, ambiguous displays, exact package");
}
