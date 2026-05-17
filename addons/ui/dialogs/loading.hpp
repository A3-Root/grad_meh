#define DIALOG_WIDTH ((LOADING_STEP_W + SPACING * 2) * GRID_W)
#define DIALOG_HEIGHT (90 * GRID_H)
#define DIALOG_TITLE "Gruppe Adler MEH"
#define DIALOG_ONLY_CLOSE true

class grad_meh_loading {
	idd = -1;
	movingEnable = 0;
	onLoad = "uiNamespace setVariable ['grad_meh_loadingDisplay', (_this select 0)];";
	onUnLoad = "call (uiNamespace getVariable 'grad_meh_fnc_loading_onUnLoad');";
	#include "base\start.hpp"
	#include "base\end.hpp"
};

#undef DIALOG_WIDTH
#undef DIALOG_HEIGHT
#undef DIALOG_TITLE
#undef DIALOG_ONLY_CLOSE
