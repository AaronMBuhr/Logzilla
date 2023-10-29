/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

using SyslogAgent.Config;

namespace SyslogAgent.ConfigUnitTest {
    public class TestValidatedString : IValidatedStringView {
        public string Content { get; set; }
        public bool IsValid { get; set; }
    }
}
