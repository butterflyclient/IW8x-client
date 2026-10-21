#pragma once
#include "engine/addresses.hpp"

#include <span>
namespace iw8x::gsc::internal::replay
{
	struct site
	{
		uintptr_t rva;
		std::span<const uint8_t> bytes;
	};
	namespace layout = game::addresses::gsc_api::layout;
	inline constexpr site ScriptContext_Server{ game::addresses::gsc_api::ScriptContext_Server, {} };
	inline constexpr site Scr_BeginLoadScripts{ game::addresses::gsc_api::Scr_BeginLoadScripts, {} };
	inline constexpr site Scr_EndLoadScripts{ game::addresses::gsc_api::Scr_EndLoadScripts, {} };
	inline constexpr site Scr_LoadScript{ game::addresses::gsc_api::Scr_LoadScript, {} };
	inline constexpr site Scr_GetFunctionHandle{ game::addresses::gsc_api::Scr_GetFunctionHandle, {} };
	inline constexpr site Scr_ExecThread{ game::addresses::gsc_api::Scr_ExecThread, {} };
	inline constexpr site Scr_ExecThreadInternal{ game::addresses::gsc_api::Scr_ExecThreadInternal, {} };
	inline constexpr site Scr_FreeThread{ game::addresses::gsc_api::Scr_FreeThread, {} };
	inline constexpr site Scr_AddInt{ game::addresses::gsc_api::Scr_AddInt, {} };
	inline constexpr site Scr_AddFloat{ game::addresses::gsc_api::Scr_AddFloat, {} };
	inline constexpr site Scr_AddString{ game::addresses::gsc_api::Scr_AddString, {} };
	inline constexpr site Scr_AddUndefined{ game::addresses::gsc_api::Scr_AddUndefined, {} };
	inline constexpr site Scr_AddVector{ game::addresses::gsc_api::Scr_AddVector, {} };
	inline constexpr site Scr_AddEntityNum{ game::addresses::gsc_api::Scr_AddEntityNum, {} };
	inline constexpr site Scr_AddArray{ game::addresses::gsc_api::Scr_AddArray, {} };
	inline constexpr site Scr_MakeArray{ game::addresses::gsc_api::Scr_MakeArray, {} };
	inline constexpr site Scr_MakeStruct{ game::addresses::gsc_api::Scr_MakeStruct, {} };
	inline constexpr site Scr_AddStructField{ game::addresses::gsc_api::Scr_AddStructField, {} };
	inline constexpr site Scr_GetNumParam{ game::addresses::gsc_api::Scr_GetNumParam, {} };
	inline constexpr site Scr_GetInt{ game::addresses::gsc_api::Scr_GetInt, {} };
	inline constexpr site Scr_GetFloat{ game::addresses::gsc_api::Scr_GetFloat, {} };
	inline constexpr site Scr_GetString{ game::addresses::gsc_api::Scr_GetString, {} };
	inline constexpr site Scr_GetConstString{ game::addresses::gsc_api::Scr_GetConstString, {} };
	inline constexpr site Scr_GetType{ game::addresses::gsc_api::Scr_GetType, {} };
	inline constexpr site Scr_GetPointerType{ game::addresses::gsc_api::Scr_GetPointerType, {} };
	inline constexpr site Scr_GetVector{ game::addresses::gsc_api::Scr_GetVector, {} };
	inline constexpr site Scr_GetEntityRef{ game::addresses::gsc_api::Scr_GetEntityRef, {} };
	inline constexpr site Scr_NotifyNum{ game::addresses::gsc_api::Scr_NotifyNum, {} };
	inline constexpr site Scr_NotifyLevel{ game::addresses::gsc_api::Scr_NotifyLevel, {} };
	inline constexpr site Scr_NotifyId{ game::addresses::gsc_api::Scr_NotifyId, {} };
	inline constexpr site SL_GetString{ game::addresses::gsc_api::SL_GetString, {} };
	inline constexpr site SL_ConvertToString{ game::addresses::gsc_api::SL_ConvertToString, {} };
	inline constexpr site Scr_ClearOutParams{ game::addresses::gsc_api::Scr_ClearOutParams, {} };
	inline constexpr site Scr_IncInParam{ game::addresses::gsc_api::Scr_IncInParam, {} };
	inline constexpr site GetObjectType{ game::addresses::gsc_api::GetObjectType, {} };
	inline constexpr site RemoveRefToObject{ game::addresses::gsc_api::RemoveRefToObject, {} };
	inline constexpr site RemoveRefToValue{ game::addresses::gsc_api::RemoveRefToValue, {} };
	inline constexpr site AddRefToObject{ game::addresses::gsc_api::AddRefToObject, {} };
	inline constexpr site AllocObject{ game::addresses::gsc_api::AllocObject, {} };
	inline constexpr site AllocThread{ game::addresses::gsc_api::AllocThread, {} };
	inline constexpr site Scr_AllocArray{ game::addresses::gsc_api::Scr_AllocArray, {} };
	inline constexpr site GetVariable{ game::addresses::gsc_api::GetVariable, {} };
	inline constexpr site GetNewArrayVariable{ game::addresses::gsc_api::GetNewArrayVariable, {} };
	inline constexpr site SetNewVariableValue{ game::addresses::gsc_api::SetNewVariableValue, {} };
	inline constexpr site FindFirstSibling{ game::addresses::gsc_api::FindFirstSibling, {} };
	inline constexpr site FindNextSibling{ game::addresses::gsc_api::FindNextSibling, {} };
	inline constexpr site GetVariableName{ game::addresses::gsc_api::GetVariableName, {} };
	inline constexpr site Scr_GetArraySize{ game::addresses::gsc_api::Scr_GetArraySize, {} };
	inline constexpr site Scr_EvalVariable{ game::addresses::gsc_api::Scr_EvalVariable, {} };
	inline constexpr site EntityRef_layout{ game::addresses::gsc_api::EntityRef_layout, {} };
	inline constexpr site VM_Execute{ game::addresses::gsc_api::VM_Execute, {} };
	inline constexpr site VM_Interpreter{ game::addresses::gsc_api::VM_Interpreter, {} };
	inline constexpr site Scr_SetBuiltinTables_Server{ game::addresses::gsc_api::Scr_SetBuiltinTables_Server, {} };
	inline constexpr site Scr_GetFunctionFromTable{ game::addresses::gsc_api::Scr_GetFunctionFromTable, {} };
	inline constexpr site Scr_GetMethodFromTable{ game::addresses::gsc_api::Scr_GetMethodFromTable, {} };
	inline constexpr site GScriptMP_GetFunction{ game::addresses::gsc_api::GScriptMP_GetFunction, {} };
	inline constexpr site GScriptMP_GetMethod{ game::addresses::gsc_api::GScriptMP_GetMethod, {} };
	inline constexpr site Dvar_FindVarByName{ game::addresses::gsc_api::Dvar_FindVarByName, {} };
	inline constexpr site Dvar_GetString{ game::addresses::gsc_api::Dvar_GetString, {} };
	inline constexpr site Dvar_GetVariantString{ game::addresses::gsc_api::Dvar_GetVariantString, {} };
	inline constexpr site Dvar_GetIntSafe{ game::addresses::gsc_api::Dvar_GetIntSafe, {} };
	inline constexpr site Dvar_GetBoolSafe{ game::addresses::gsc_api::Dvar_GetBoolSafe, {} };
	inline constexpr site Dvar_GetFloatSafe{ game::addresses::gsc_api::Dvar_GetFloatSafe, {} };
	inline constexpr site GScr_FreeEntity{ game::addresses::gsc_api::GScr_FreeEntity, {} };
	inline constexpr site GScr_Notify{ game::addresses::gsc_api::GScr_Notify, {} };
	inline constexpr site Scr_FreeEntityNum{ game::addresses::gsc_api::Scr_FreeEntityNum, {} };

	namespace weapon
	{
		inline constexpr site AddParam{ game::addresses::gsc_api::Weapon_AddParam, {} };
		inline constexpr site GetWeapon{ game::addresses::gsc_api::Weapon_GetWeapon, {} };
		inline constexpr site IsAlternate{ game::addresses::gsc_api::Weapon_IsAlternate, {} };
		inline constexpr auto entity_class = game::addresses::gsc_api::weapon_entity_class;
		inline constexpr auto descriptor_size = game::addresses::gsc_api::weapon_descriptor_size;
		inline constexpr auto alternate = game::addresses::gsc_api::weapon_snapshot_alternate;
		inline constexpr auto reserved = game::addresses::gsc_api::weapon_snapshot_reserved;
	}
}
