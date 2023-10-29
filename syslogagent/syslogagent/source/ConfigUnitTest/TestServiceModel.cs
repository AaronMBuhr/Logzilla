/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

using System;
using SyslogAgent.Config;

namespace SyslogAgent.ConfigUnitTest {
    public class TestServiceModel: ServiceModel {
        public string Status { set; get; }
        public void Restart(Action<string> showStatus) {
            showStatus("very " + Status);
            IsDone = true;
        }
        public bool IsDone;
    }
}
