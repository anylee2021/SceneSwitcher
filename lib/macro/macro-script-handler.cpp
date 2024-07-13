#include "macro-script-handler.hpp"
#include "macro-action-script.hpp"
#include "plugin-state-helpers.hpp"
#include "log-helper.hpp"

#include <obs.hpp>

namespace advss {

#define RETURN_STATUS(status)                               \
	{                                                   \
		calldata_set_bool(data, "success", status); \
		return;                                     \
	}
#define RETURN_SUCCESS() RETURN_STATUS(true);
#define RETURN_FAILURE() RETURN_STATUS(false);

static constexpr std::string_view nameParam = "name";
static constexpr std::string_view blockingParam = "blocking";
static constexpr std::string_view triggerSignalParam = "trigger_signal_name";
static constexpr std::string_view registerActionFuncName =
	"advss_register_script_action";
static constexpr std::string_view deregisterActionFuncName =
	"advss_deregister_script_action";

static const std::string registerScriptActionDeclString =
	std::string("bool ") + registerActionFuncName.data() + "(in string " +
	nameParam.data() + ", in bool " + blockingParam.data() +
	", out string " + triggerSignalParam.data() + ", out string " +
	GetCompletionSignalParamName().data() + ")";
static const std::string deregisterScriptActionDeclString =
	std::string("bool ") + deregisterActionFuncName.data() + "(in string " +
	nameParam.data() + ")";

static bool setup();
static bool setupDone = setup();

static bool setup()
{
	AddPluginInitStep([]() { new ScriptHandler(); });
	return true;
}

ScriptHandler::ScriptHandler()
{
	proc_handler_t *ph = obs_get_proc_handler();
	assert(ph != NULL);

	proc_handler_add(ph, registerScriptActionDeclString.c_str(),
			 &RegisterScriptAction, this);
	proc_handler_add(ph, registerScriptActionDeclString.c_str(),
			 &RegisterScriptAction, this);
}

static void replaceWithspace(std::string &string)
{
	std::transform(string.begin(), string.end(), string.begin(), [](char c) {
		return std::isspace(static_cast<unsigned char>(c)) ? '_' : c;
	});
}

static std::string nameToScriptID(const std::string &name)
{
	return std::string("script_") + name;
}

void ScriptHandler::RegisterScriptAction(void *ctx, calldata_t *data)
{
	auto handler = static_cast<ScriptHandler *>(ctx);
	const char *actionName;
	if (!calldata_get_string(data, nameParam.data(), &actionName) ||
	    strlen(actionName) == 0) {
		blog(LOG_WARNING, "[%s] failed! \"%s\" parameter missing!",
		     registerScriptActionDeclString.data(), nameParam.data());
		RETURN_FAILURE();
	}
	bool blocking;
	if (!calldata_get_bool(data, blockingParam.data(), &blocking)) {
		blog(LOG_WARNING, "[%s] failed! \"%s\" parameter missing!",
		     registerActionFuncName.data(), blockingParam.data());
		RETURN_FAILURE();
	}

	std::lock_guard<std::mutex> lock(handler->_mutex);

	if (handler->_actions.count(actionName) > 0) {
		blog(LOG_WARNING, "[%s] failed! Action \"%s\" already exists!",
		     registerActionFuncName.data(), actionName);
		RETURN_FAILURE();
	}

	const std::string id = nameToScriptID(actionName);

	std::string signalName = std::string(actionName);
	replaceWithspace(signalName);
	signalName += "_perform_action";
	std::string completionSignalName = signalName + "_complete";

	const auto createScriptAction =
		[id, blocking, signalName, completionSignalName](
			Macro *m) -> std::shared_ptr<MacroAction> {
		return std::make_shared<MacroActionScript>(
			m, id, blocking, signalName, completionSignalName);
	};
	if (!MacroActionFactory::Register(id, {createScriptAction,
					       MacroActionScriptEdit::Create,
					       actionName})) {
		blog(LOG_WARNING,
		     "[%s] failed! Action id \"%s\" already exists!",
		     registerActionFuncName.data(), id.c_str());
		RETURN_FAILURE();
	}

	blog(LOG_INFO, "[%s] successful for \"%s\"",
	     registerActionFuncName.data(), actionName);

	calldata_set_string(data, triggerSignalParam.data(),
			    signalName.c_str());
	handler->_actions.emplace(id, ScriptAction(id, blocking, signalName,
						   completionSignalName));

	RETURN_SUCCESS();
}

void ScriptHandler::DeregisterScriptAction(void *ctx, calldata_t *data)
{
	auto handler = static_cast<ScriptHandler *>(ctx);
	const char *actionName;
	if (!calldata_get_string(data, nameParam.data(), &actionName) ||
	    strlen(actionName) == 0) {
		blog(LOG_WARNING, "[%s] failed! \"%s\" parameter missing!",
		     deregisterActionFuncName.data(), nameParam.data());
		RETURN_FAILURE();
	}

	const std::string id = nameToScriptID(actionName);
	std::lock_guard<std::mutex> lock(handler->_mutex);

	if (handler->_actions.count(actionName) == 0) {
		blog(LOG_WARNING, "[%s] failed! Action \"%s\" already exists!",
		     deregisterActionFuncName.data(), actionName);
		RETURN_FAILURE();
	}

	if (!MacroActionFactory::Deregister(id)) {
		blog(LOG_WARNING,
		     "[%s] failed! Action id \"%s\" already exists!",
		     deregisterActionFuncName.data(), id.c_str());
		RETURN_FAILURE();
	}

	RETURN_SUCCESS();
}

static std::string signalNameToSignalDecl(const std::string &name)
{
	return std::string("void ") + name + "()";
}

ScriptAction::ScriptAction(const std::string &id, bool blocking,
			   const std::string &signal,
			   const std::string &signalComplete)
	: _id(id)
{
	signal_handler_add(obs_get_signal_handler(),
			   signalNameToSignalDecl(signal).c_str());
	if (!blocking) {
		return;
	}
	signal_handler_add(obs_get_signal_handler(),
			   signalNameToSignalDecl(signalComplete).c_str());
}

} // namespace advss
