/*
 * Author: DerZade
 * Start export of given maps with given params
 *
 * Arguments:
 * 0: Maps to export <ARRAY>
 * 1: Export sat images (Optional, Default: true) <BOOLEAN>
 * 2: Export topographic image (Optional, Default: true) <BOOLEAN>
 * 3: Export baked topographic image (Optional, Default: true) <BOOLEAN>
 * 4: Export houses / locations (Optional, Default: true) <BOOLEAN>
 * 5: Export preview image (Optional, Default: true) <BOOLEAN>
 * 6: Export meta.json (Optional, Default: true) <BOOLEAN>
 * 7: Export digital elevation model (Optional, Default: true) <BOOLEAN>
 * 8: Export Arma diagnostic map SVG topo source (Optional, Default: true) <BOOLEAN>
 *
 * Return Value:
 * NONE
 *
 * Example:
 * [["Stratis"]] call grad_meh_fnc_export;
 *
 * Public: No
 */

#include "../status_codes.hpp"

params [
	["_maps", [], []],
	["_exportSat", true, [true]],
	["_exportTopo", true, [true]],
	["_exportBakedTopo", true, [true]],
	["_exportHouses", true, [true]],
	["_exportPreviewImg", true, [true]],
	["_exportMeta", true, [true]],
	["_exportDem", true, [true]],
	["_exportArmaTopo", true, [true]]
];

// reset progess
uiNamespace setVariable ["grad_meh_progress", []];
uiNamespace setVariable ["grad_meh_errors", []];

private _cancelSteps = {
	params ["_step", "_maps"];
	{
		[_x, _step, "canceled"] call (uiNamespace getVariable "grad_meh_fnc_updateProgress");
	} forEach _maps;
};

// set step to canceled if it is turned off
if (!_exportSat) then { ["write_sat", _maps] call _cancelSteps; };
if (!_exportTopo) then { ["write_topo", _maps] call _cancelSteps; };
if (!_exportBakedTopo) then { ["write_baked_topo", _maps] call _cancelSteps; };
if (!_exportHouses) then { ["write_houses", _maps] call _cancelSteps; };
if (!_exportPreviewImg) then { ["write_preview", _maps] call _cancelSteps; };
if (!_exportMeta) then { ["write_meta", _maps] call _cancelSteps; };
if (!_exportDem) then { ["write_dem", _maps] call _cancelSteps; };
if (!_exportArmaTopo) then { ["write_arma_topo", _maps] call _cancelSteps; };

uiNamespace setVariable ["grad_meh_exportOptions", +_this];
uiNamespace setVariable ["grad_meh_exportPhase", ["capture_svg", "bulk_export"] select (!_exportArmaTopo || {_maps isEqualTo []})];
uiNamespace setVariable ["grad_meh_exportMapIndex", 0];

disableSerialization;

uiNamespace setVariable ["grad_meh_fnc_exportStartMission", {
	params [["_world", "VR"]];

	playScriptedMission [
		_world,
		{
			[] spawn {
				#include "../status_codes.hpp"

				private _options = uiNamespace getVariable ["grad_meh_exportOptions", []];
				_options params [
					["_maps", [], []],
					["_exportSat", true, [true]],
					["_exportTopo", true, [true]],
					["_exportBakedTopo", true, [true]],
					["_exportHouses", true, [true]],
					["_exportPreviewImg", true, [true]],
					["_exportMeta", true, [true]],
					["_exportDem", true, [true]],
					["_exportArmaTopo", true, [true]]
				];

				private _closeAndEndMission = {
					private _zero = findDisplay(0);
					{
						if (_x != _zero) then {
							_x closeDisplay 1;
						};
					} forEach allDisplays;
					failMission "END1";
				};

				private _reportError = {
					params [["_world", ""], ["_error", ""]];
					diag_log format ["[GRAD_MEH]: Error while exporting map %1: %2", _world, _error];
					private _errors = uiNamespace getVariable ["grad_meh_errors", []];
					_errors = [_errors, _world, _error, true] call (uiNamespace getVariable "BIS_fnc_setToPairs");
					uiNamespace setVariable ["grad_meh_errors", _errors];
					private _loadingDisplay = uiNamespace getVariable ["grad_meh_loadingDisplay", displayNull];
					if !(isNull _loadingDisplay) then {
						[_loadingDisplay] call (uiNamespace getVariable "grad_meh_fnc_loading_redraw");
					};
				};

				waitUntil { !isNull findDisplay 46 };

				private _loadingDisplay = (findDisplay 46) createDisplay "grad_meh_loading";
				_loadingDisplay setVariable ["grad_meh_worlds", _maps];
				[_loadingDisplay] call (uiNamespace getVariable "grad_meh_fnc_loading_redraw");

				private _phase = uiNamespace getVariable ["grad_meh_exportPhase", "bulk_export"];
				if (_phase isEqualTo "capture_svg") exitWith {
					private _index = uiNamespace getVariable ["grad_meh_exportMapIndex", 0];
					private _currentMap = _maps select _index;

					if (isNil "BIS_fnc_diagRadio") then {
						{
							[_x, "write_arma_topo", "canceled"] call (uiNamespace getVariable "grad_meh_fnc_updateProgress");
						} forEach _maps;
						[_currentMap, "Arma map SVG export requires the diagnostic executable / development branch."] call _reportError;
						uiNamespace setVariable ["grad_meh_exportPhase", "bulk_export"];
						["VR"] call (uiNamespace getVariable "grad_meh_fnc_exportStartMission");
						call _closeAndEndMission;
					};

					[_currentMap, "write_arma_topo", "running"] call (uiNamespace getVariable "grad_meh_fnc_updateProgress");
					private _svgPath = call compile ("gradMehPrepareArmaTopoSvg " + str _currentMap);
					if (_svgPath isEqualType "" && {_svgPath isNotEqualTo ""}) then {
						call compile ("diag_exportTerrainSVG [" + str _svgPath + ", true, false, true, true, true, false]");
						[_currentMap, "write_arma_topo", "done"] call (uiNamespace getVariable "grad_meh_fnc_updateProgress");
					} else {
						[_currentMap, "write_arma_topo", "canceled"] call (uiNamespace getVariable "grad_meh_fnc_updateProgress");
						[_currentMap, "Could not prepare Arma map SVG output path."] call _reportError;
					};

					private _nextIndex = _index + 1;
					uiNamespace setVariable ["grad_meh_exportMapIndex", _nextIndex];
					if (_nextIndex < count _maps) then {
						[_maps select _nextIndex] call (uiNamespace getVariable "grad_meh_fnc_exportStartMission");
					} else {
						uiNamespace setVariable ["grad_meh_exportPhase", "bulk_export"];
						["VR"] call (uiNamespace getVariable "grad_meh_fnc_exportStartMission");
					};
					call _closeAndEndMission;
				};

				{
					private _startedOrAborted = false;
					while { !_startedOrAborted } do {
						private _exportArgs = [
							_x,
							_exportSat,
							_exportTopo,
							_exportBakedTopo,
							_exportHouses,
							_exportPreviewImg,
							_exportMeta,
							_exportDem,
							_exportArmaTopo
						];

						private _status = call compile ("gradMehExportMap " + str _exportArgs);

						switch (_status) do {
							case GRAD_MEH_STATUS_OK: { _startedOrAborted = true; };
							case GRAD_MEH_STATUS_ERR_ARGS: {
								_startedOrAborted = true;
								[_x, "Couldn't export map, because something went wrong when passing arguments."] call _reportError;
							};
							case GRAD_MEH_STATUS_ERR_ALREADY_RUNNING: { /* Just wait and try again next cycle */ };
							case GRAD_MEH_STATUS_ERR_NOT_FOUND: {
								_startedOrAborted = true;
								[_x, "Couldn't export map, because it wasn't found in configFile."] call _reportError;
							};
							case GRAD_MEH_STATUS_ERR_NO_WORLD_SIZE: {
								_startedOrAborted = true;
								[_x, "Couldn't export map, because worldSize is missing in its config."] call _reportError;
							};
							case GRAD_MEH_STATUS_ERR_PBO_NOT_FOUND: {
								_startedOrAborted = true;
								[_x, "Couldn't export map, because the PBO of WRP couldn't be found. (Most likely because it is a EBO)"] call _reportError;
							};
							case GRAD_MEH_STATUS_ERR_PBO_POPULATING: { /* Just wait and try again next cycle */ };
							default {
								_startedOrAborted = true;
								[_x, "An unknown error occurred, while exporting the map."] call _reportError;
							};
						};

						sleep 10;
					};
				} forEach _maps;

				uiNamespace setVariable ["grad_meh_exportOptions", nil];
				uiNamespace setVariable ["grad_meh_exportPhase", nil];
				uiNamespace setVariable ["grad_meh_exportMapIndex", nil];
			};
		},
		missionConfigFile,
		true
	];
}];

private _initialWorld = ["VR", _maps select 0] select (_exportArmaTopo && {_maps isNotEqualTo []});
[_initialWorld] call (uiNamespace getVariable "grad_meh_fnc_exportStartMission");

// This is needed for playScriptedMission to work properly
//Close all displays that could be the background display ... this is essentialy forceEnd command
//Closing #0 will cause game to fail
private _zero = findDisplay(0);
{
	if (_x != _zero) then {
		_x closeDisplay 1;
	};
} forEach allDisplays;

failMission "END1";
