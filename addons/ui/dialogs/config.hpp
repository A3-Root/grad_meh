#define ROW_HEIGHT 3
#define ROW_Y(index) ((SPACING + index * ROW_HEIGHT) * GRID_H)
#define DIALOG_WIDTH (50 * GRID_W)
#define DIALOG_HEIGHT (ROW_Y(7) + SPACING * GRID_H)
#define DIALOG_TITLE "Gruppe Adler MEH"
#define DIALOG_NON_SCROLLABLE true

class grad_meh_config {
	idd = -1;
	movingEnable = 0;
	onLoad = "call (uiNamespace getVariable 'grad_meh_fnc_config_onLoad');";
	onUnLoad = "call (uiNamespace getVariable 'grad_meh_fnc_config_onUnLoad');";
	#include "base\start.hpp"
	class sat: ctrlControlsGroupNoScrollbars {
		x = QUOTE(SPACING * GRID_W);
		y = QUOTE(ROW_Y(0));
		w = QUOTE(DIALOG_WIDTH);
		h = QUOTE(ROW_HEIGHT * GRID_H);
		idc = -1;
		class Controls {
			class check: ctrlCheckbox {
				idc = IDC_CONFIG_CHECK_SAT;
				x = 0;
				y = QUOTE(ROW_HEIGHT * 0.05 * GRID_H);
				w = QUOTE(ROW_HEIGHT * 0.9 * GRID_W);
				h = QUOTE(ROW_HEIGHT * 0.9 * GRID_H);
				checked = 1;
			};
			class text: ctrlStatic {
				idc = -1;
				x = QUOTE(ROW_HEIGHT * GRID_W);
				y = 0;
				w = QUOTE(safezoneW - ROW_HEIGHT * GRID_W);
				h = QUOTE(ROW_HEIGHT * GRID_H);
				text = "Export satellite image";
				sizeEx = QUOTE(ROW_HEIGHT * 0.9 * GRID_H);
			};
		};
	};
	class topo: sat {
		y = QUOTE(ROW_Y(1));
		class Controls: Controls {
			class check: check {
				idc = IDC_CONFIG_CHECK_TOPO;
			};
			class text: text {
				text = "Export topographic image";
			};
		};
	};
	class bakedTopo: sat {
		y = QUOTE(ROW_Y(2));
		class Controls: Controls {
			class check: check {
				idc = IDC_CONFIG_CHECK_BAKEDTOPO;
			};
			class text: text {
				text = "Export baked topography";
			};
		};
	};
	class houses: sat {
		y = QUOTE(ROW_Y(3));
		class Controls: Controls {
			class check: check {
				idc = IDC_CONFIG_CHECK_HOUSES;
			};
			class text: text {
				text = "Export geojsons";
			};
		};
	};
	class preview: sat {
		y = QUOTE(ROW_Y(4));
		class Controls: Controls {
			class check: check {
				idc = IDC_CONFIG_CHECK_PREVIEW;
			};
			class text: text {
				text = "Export preview image";
			};
		};
	};
	class meta: sat {
		y = QUOTE(ROW_Y(5));
		class Controls: Controls {
			class check: check {
				idc = IDC_CONFIG_CHECK_META;
			};
			class text: text {
				text = "Export meta.json";
			};
		};
	};
	class dem: sat {
		y = QUOTE(ROW_Y(6));
		class Controls: Controls {
			class check: check {
				idc = IDC_CONFIG_CHECK_DEM;
			};
			class text: text {
				text = "Export digital elevation model";
			};
		};
	};
	#include "base\end.hpp"
};

#undef ROW_HEIGHT
#undef ROW_Y
#undef DIALOG_WIDTH
#undef DIALOG_HEIGHT
#undef DIALOG_TITLE
#undef DIALOG_NON_SCROLLABLE
