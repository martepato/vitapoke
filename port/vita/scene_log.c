/* vitapoke: a line in the log every time the game changes scene.
 *
 * Every screen in this game is entered through ApplicationManager_New with a known template, so
 * wrapping that one call gives exact scene markers for nothing per frame. It is the single most
 * useful thing in the log when a build gets somewhere and then stops: it says where.
 *
 * The PSP build carried this in its memory probe, together with a substitution that replaced the
 * game's own name-entry screen with the PSP firmware keyboard -- that screen black-screened a real
 * PSP-3001. Nothing here needs that: the Vita's touch screen is a touch screen, so the DS's naming
 * screen is used as the game shipped it.
 */
#include <stdio.h>

#include "include/vita_os.h"

extern void VitaNativeMemReport(const char *tag);
extern void *__real_ApplicationManager_New(const void *tmpl, void *args, int heapID);

/* The templates are plain symbols in the game's own data; naming them needs no game headers. */
#define TEMPLATE(name) extern const char name[];
TEMPLATE(gTitleScreenAppTemplate) TEMPLATE(gMainMenuAppTemplate) TEMPLATE(gOpeningCutsceneAppTemplate)
TEMPLATE(gGameStartNewSaveAppTemplate) TEMPLATE(gGameStartLoadSaveAppTemplate)
TEMPLATE(gGameStartRowanIntroAppTemplate) TEMPLATE(gRowanIntroAppTemplate)
TEMPLATE(gNamingScreenAppTemplate) TEMPLATE(gPokemonPartyAppTemplate)
TEMPLATE(gFieldMapTemplate) TEMPLATE(gFieldSystemNewGameTemplate)
#undef TEMPLATE

static const char *TemplateName(const void *t)
{
#define MATCH(name) if ((const char *)t == name) return #name;
	MATCH(gTitleScreenAppTemplate) MATCH(gMainMenuAppTemplate) MATCH(gOpeningCutsceneAppTemplate)
	MATCH(gGameStartNewSaveAppTemplate) MATCH(gGameStartLoadSaveAppTemplate)
	MATCH(gGameStartRowanIntroAppTemplate) MATCH(gRowanIntroAppTemplate)
	MATCH(gNamingScreenAppTemplate) MATCH(gPokemonPartyAppTemplate)
	MATCH(gFieldMapTemplate) MATCH(gFieldSystemNewGameTemplate)
#undef MATCH
	return NULL;
}

void *__wrap_ApplicationManager_New(const void *tmpl, void *args, int heapID)
{
	const char *name = TemplateName(tmpl);
	char tag[64];

	if (name)
		snprintf(tag, sizeof tag, "enter %s", name);
	else
		snprintf(tag, sizeof tag, "enter app@%p", tmpl);
	VitaNativeMemReport(tag);
	return __real_ApplicationManager_New(tmpl, args, heapID);
}
