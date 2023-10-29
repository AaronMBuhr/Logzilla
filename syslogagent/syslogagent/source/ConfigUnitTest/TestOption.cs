/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

using SyslogAgent.Config;

namespace SyslogAgent.ConfigUnitTest {
    public class TestOption : IOptionView {
        public bool IsSelected { get; set; }
    }
}
