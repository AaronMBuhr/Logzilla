/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

using SyslogAgent.Config;

namespace SyslogAgent.ConfigUnitTest {
    public class TestString : IStringView {
        public string Content { get; set; }
    }
}
