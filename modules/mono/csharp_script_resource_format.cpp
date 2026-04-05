/**************************************************************************/
/*  csharp_script_resource_format.cpp                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "csharp_script_resource_format.h"

#include "mono_gd/gd_mono_cache.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"

#ifdef TOOLS_ENABLED
static bool _create_project_solution_if_needed() {
	CRASH_COND(CSharpLanguage::get_singleton()->get_godotsharp_editor() == nullptr);
	return CSharpLanguage::get_singleton()->get_godotsharp_editor()->call("CreateProjectSolutionIfNeeded");
}
#endif

Ref<Resource> ResourceFormatLoaderCSharpScript::load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) {
	if (r_error) {
		*r_error = ERR_FILE_CANT_OPEN;
	}

	// TODO ignore anything inside bin/ and obj/ in tools builds?

	String real_path = p_path;
	if (p_path.begins_with("csharp://")) {
		// This is a virtual path used by generic types, extract the real path.
		real_path = "res://" + p_path.trim_prefix("csharp://");
		real_path = real_path.substr(0, real_path.rfind_char(':'));
	}

	Ref<CSharpScript> scr;

	if (GDMonoCache::godot_api_cache_updated) {
		GDMonoCache::managed_callbacks.ScriptManagerBridge_GetOrCreateScriptBridgeForPath(&p_path, &scr);
		ERR_FAIL_COND_V_MSG(scr.is_null(), Ref<Resource>(), "Could not create C# script '" + real_path + "'.");
	} else {
		scr.instantiate();
	}

#ifdef DEBUG_ENABLED
	Error err = scr->load_source_code(real_path);
	ERR_FAIL_COND_V_MSG(err != OK, Ref<Resource>(), "Cannot load C# script file '" + real_path + "'.");
#endif // DEBUG_ENABLED

	// Only one instance of a C# script is allowed to exist.
	ERR_FAIL_COND_V_MSG(!scr->get_path().is_empty() && scr->get_path() != p_original_path, Ref<Resource>(),
			"The C# script path is different from the path it was registered in the C# dictionary.");

	Ref<Resource> existing = ResourceCache::get_ref(p_path);
	switch (p_cache_mode) {
		case ResourceFormatLoader::CACHE_MODE_IGNORE:
		case ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP:
			break;
		case ResourceFormatLoader::CACHE_MODE_REUSE:
			if (existing.is_null()) {
				scr->set_path(p_original_path);
			} else {
				scr = existing;
			}
			break;
		case ResourceFormatLoader::CACHE_MODE_REPLACE:
		case ResourceFormatLoader::CACHE_MODE_REPLACE_DEEP:
			scr->set_path(p_original_path, true);
			break;
	}

	scr->reload();

	if (r_error) {
		*r_error = OK;
	}

	return scr;
}

void ResourceFormatLoaderCSharpScript::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("cs");
}

bool ResourceFormatLoaderCSharpScript::handles_type(const String &p_type) const {
	return p_type == "Script" || p_type == CSharpLanguage::get_singleton()->get_type();
}

String ResourceFormatLoaderCSharpScript::get_resource_type(const String &p_path) const {
	return p_path.has_extension("cs") ? CSharpLanguage::get_singleton()->get_type() : "";
}

#define UID_COMMENT_PREFIX "// uid://"
#define UID_COMMENT_SUFFIX "This line is generated, don't modify or remove it."
static ResourceUID::ID extract_uid_from_line(const String &p_line) {
	Vector<String> splits = p_line.strip_edges().substr(3).split(" ", false, 1);
	if (splits.is_empty()) {
		return ResourceUID::INVALID_ID;
	}
	return ResourceUID::get_singleton()->text_to_id(splits[0]);
}

ResourceUID::ID ResourceFormatLoaderCSharpScript::get_resource_uid(const String &p_path) const {
	int64_t uid = ResourceUID::INVALID_ID;

	if (FileAccess::exists(p_path + ".uid")) {
		Ref<FileAccess> file = FileAccess::open(p_path + ".uid", FileAccess::READ);
		if (file.is_valid()) {
			uid = ResourceUID::get_singleton()->text_to_id(file->get_line());
		}
	} else {
		const String extension = p_path.get_extension().to_lower();
		if (extension == "cs") {
			Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
			if (file.is_valid()) {
				while (!file->eof_reached()) {
					String line = file->get_line().strip_edges();
					if (!line.is_empty()) {
						if (line.begins_with(UID_COMMENT_PREFIX)) {
							uid = extract_uid_from_line(line);
						}
						break;
					}
				}
			}
		}
	}

	return uid;
}

bool ResourceFormatLoaderCSharpScript::has_custom_uid_support() const {
	return GLOBAL_GET("filesystem/inline_text_resource_uids/c_sharp_script");
}

ResourceFormatLoaderCSharpScript::ResourceFormatLoaderCSharpScript() {
	GLOBAL_DEF("filesystem/inline_text_resource_uids/c_sharp_script", true);
}

bool ResourceFormatSaverCSharpScript::add_uid_to_source(String &p_r_source, const String &p_path, ResourceUID::ID p_uid) const {
	bool need_update = false;
	Vector<String> lines = p_r_source.split("\n");
	bool uid_comment_valid = false;
	for (Vector<String>::Size i = 0; i < lines.size(); i++) {
		const String &line = lines[i].strip_edges();
		if (line.begins_with(UID_COMMENT_PREFIX)) {
			ResourceUID::ID uid = extract_uid_from_line(line);
			if (uid == ResourceUID::INVALID_ID || p_uid != uid) {
				if (p_uid == ResourceUID::INVALID_ID) {
					p_uid = ResourceSaver::get_resource_id_for_path(p_path, true);
				}

				if (uid != p_uid) {
					lines.set(i, vformat("// %s %s", ResourceUID::get_singleton()->id_to_text(uid), UID_COMMENT_SUFFIX));
					if (ResourceUID::get_singleton()->has_id(uid)) {
						ResourceUID::get_singleton()->set_id(uid, p_path);
					} else {
						ResourceUID::get_singleton()->add_id(uid, p_path);
					}
					need_update = true;
				}
			}

			uid_comment_valid = true;
			break;
		} else if (!line.strip_edges().is_empty()) {
			break;
		}
	}

	if (!uid_comment_valid) {
		ResourceUID::ID uid = p_uid == ResourceUID::INVALID_ID ? ResourceSaver::get_resource_id_for_path(p_path, true) : p_uid;
		lines.insert(0, vformat("// %s %s", ResourceUID::get_singleton()->id_to_text(uid), UID_COMMENT_SUFFIX));
		if (ResourceUID::get_singleton()->has_id(uid)) {
			ResourceUID::get_singleton()->set_id(uid, p_path);
		} else {
			ResourceUID::get_singleton()->add_id(uid, p_path);
		}
		need_update = true;
	}

	if (need_update) {
		p_r_source = String("\n").join(lines);
		return true;
	} else {
		return false;
	}
}

Error ResourceFormatSaverCSharpScript::save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	Ref<CSharpScript> sqscr = p_resource;
	ERR_FAIL_COND_V(sqscr.is_null(), ERR_INVALID_PARAMETER);

	String source = sqscr->get_source_code();

#ifdef TOOLS_ENABLED
	if (!FileAccess::exists(p_path)) {
		// The file does not yet exist, let's assume the user just created this script. In such
		// cases we need to check whether the solution and csproj were already created or not.
		if (!_create_project_solution_if_needed()) {
			ERR_PRINT("C# project could not be created; cannot add file: '" + p_path + "'.");
		}
	}
#endif

	{
		Error err;
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot save C# script file '" + p_path + "'.");

		bool source_changed = false;
		if (!FileAccess::exists(p_path + ".uid")) {
			if (p_path.begins_with("res://addons/") && GLOBAL_GET("filesystem/inline_text_resource_uids/compatibility/no_inline_text_resource_uids_in_addons")) {
				Ref<FileAccess> f = FileAccess::open(p_path + ".uid", FileAccess::WRITE);
				if (f.is_valid()) {
					ResourceUID::ID uid = ResourceUID::get_singleton()->create_id_for_path(p_path);
					f->store_line(ResourceUID::get_singleton()->id_to_text(uid));
					f->close();
				}
			} else {
				source_changed = add_uid_to_source(source, p_path);
			}
		}

		file->store_string(source);
		if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF) {
			return ERR_CANT_CREATE;
		}
		file->close();

		if (source_changed) {
			sqscr->set_source_code(source);
			sqscr->reload();
			sqscr->emit_changed();
		}
	}

#ifdef TOOLS_ENABLED
	if (ScriptServer::is_reload_scripts_on_save_enabled()) {
		CSharpLanguage::get_singleton()->reload_tool_script(p_resource);
	}
#endif

	return OK;
}

void ResourceFormatSaverCSharpScript::get_recognized_extensions(const Ref<Resource> &p_resource, List<String> *p_extensions) const {
	if (Object::cast_to<CSharpScript>(p_resource.ptr())) {
		p_extensions->push_back("cs");
	}
}

bool ResourceFormatSaverCSharpScript::recognize(const Ref<Resource> &p_resource) const {
	return Object::cast_to<CSharpScript>(p_resource.ptr()) != nullptr;
}

Error ResourceFormatSaverCSharpScript::set_uid(const String &p_path, ResourceUID::ID p_uid) {
	if (FileAccess::exists(p_path + ".uid") || (p_path.begins_with("res://addons/") && GLOBAL_GET("filesystem/inline_text_resource_uids/compatibility/no_inline_text_resource_uids_in_addons"))) {
		Ref<FileAccess> f = FileAccess::open(p_path + ".uid", FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_line(ResourceUID::get_singleton()->id_to_text(p_uid));
			return OK;
		} else {
			return FileAccess::get_open_error();
		}
	} else if (p_path.get_extension().to_lower() == "cs") {
		Error err = OK;
		String source = FileAccess::get_file_as_string(p_path, &err);
		if (err != OK) {
			return err;
		}

		const bool source_changed = add_uid_to_source(source, p_path, p_uid);
		if (source_changed) {
			Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
			if (f.is_valid()) {
				f->store_string(source);
				f->close();

				// Update source code if it's loaded.
				Ref<CSharpScript> loaded_csharp_script = ResourceCache::get_ref(p_path);
				if (loaded_csharp_script.is_valid()) {
					loaded_csharp_script->set_source_code(source);
					loaded_csharp_script->reload();
					loaded_csharp_script->emit_changed();
				}

				err = OK;
			} else {
				err = FileAccess::get_open_error();
			}
		}

		return err;
	}

	return ERR_FILE_UNRECOGNIZED;
}
