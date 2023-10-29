/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

using System.Collections.Generic;
using SyslogAgent.Config;

namespace SyslogAgent.ConfigUnitTest {
    public class TestSelectionList : ISelectionListView {
        public List<string> Names = new List<string>();
        public List<bool> IsChosens = new List<bool>();

        public int Count => Names.Count;

        public void Add(string name, bool isChosen) {
            Names.Add(name);
            IsChosens.Add(isChosen);
        }

        public bool IsChosen(int index) {
            return IsChosens[index];
        }

        public void SetIsChosen(int index, bool isChosen) {
            IsChosens[index] = isChosen;
        }
    }
}
