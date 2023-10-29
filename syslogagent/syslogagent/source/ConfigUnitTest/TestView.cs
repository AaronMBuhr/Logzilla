/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

using SyslogAgent.Config;

#if DISABLED
namespace SyslogAgent.ConfigUnitTest {
    public class TestView : MainView {
        public ValidatedStringView CarriageReturn { get; set; }
        public ValidatedStringView LineFeed { get; set; }
        public ValidatedStringView PollInterval { get; set; }
        public ValidatedStringView Tab { get; set; }
        public OptionView LookUpAccount { get; set; }
        public OptionView IncludeKeyValuePairs { get; set; }
        public OptionView SendToSecondary { get; set; }
        public OptionView UseJsonMessage { get; set; }
        public OptionView UseForwarder { get; set; }
        public StringView EventIdFilter { get; set; }
        public StringView Suffix { get; set; }
        public StringView SecondaryHost { get; set; }
        public StringView SecondaryPort { get; set; }
        public StringView PrimaryHost { get; set; }
        public StringView PrimaryPort { get; set; }
        public StringView ForwarderTcpListenPort { get; set; }
        public StringView ForwarderUdpListenPort { get; set; }
        public OptionListView Transport { get; set; }
        public OptionListView SyslogProtocol { get; set; }
        public string Message { get; set; }
        public string Status { get; set; }
        public SelectionListView Logs { get; set; }
        public OptionListView Facility { get; set; }
        public OptionListView Severity { get; set; }
    }
}
#endif
