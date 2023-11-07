/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

namespace SyslogAgent.Config {
    public interface IMainView {
        // IValidatedStringView PollInterval { get; }
        IOptionView LookUpAccount { get; }
        //IOptionView IncludeKeyValuePairs { get; }
        IOptionView SendToSecondary { get; }
        IValidatedOptionView IncludeEventIds { get; }
        IValidatedOptionView IgnoreEventIds { get; }
        IValidatedStringView EventIdFilter { get; }
        IValidatedStringView SecondaryHost { get; }
        IValidatedStringView PrimaryHost { get; }
        IValidatedOptionView PrimaryUseTls { get; }
        IValidatedOptionView SecondaryUseTls { get; }
        IValidatedStringView Suffix { get; }
        string Message { set; }
        void SetFailureMessage(string message);
        void SetSuccessMessage(string message);
        string ChooseImportFileButton();
        string ChooseExportFileButton();
        void UpdateDisplay(Configuration config);
        string Status { set; }
        string LogzillaFileVersion { set; }
        // SelectionListView Logs { get; }
        IOptionListView Facility { get; }
        IOptionListView Severity { get; }
        IOptionListView DebugLevel { get; }
        IValidatedStringView DebugLogFilename { get; }
        IValidatedStringView TailFilename { get; }
        IValidatedStringView TailProgramName { get; }
    }
}
