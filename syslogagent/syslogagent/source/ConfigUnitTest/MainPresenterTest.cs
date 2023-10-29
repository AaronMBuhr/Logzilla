/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

using System.Threading;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using SyslogAgent.Config;

#if DISABLED
namespace SyslogAgent.ConfigUnitTest {
    [TestClass]
    public class MainPresenterTest {

        [TestMethod]
        public void LoadsConfiguration() {
            serviceModel.Status = "happy";

            presenter.Load();

            Assert.AreEqual("12", view.CarriageReturn.Content);
            Assert.AreEqual("34", view.LineFeed.Content);
            Assert.AreEqual("5678", view.PollInterval.Content);
            Assert.AreEqual("90", view.Tab.Content);
            Assert.IsTrue(view.LookUpAccount.IsSelected);
            Assert.IsTrue(view.IncludeKeyValuePairs.IsSelected);
            Assert.IsTrue(view.SendToSecondary.IsSelected);
            Assert.AreEqual("filter", view.EventIdFilter.Content);
            Assert.AreEqual("suffix", view.Suffix.Content);
            Assert.AreEqual("secondhost", view.SecondaryHost.Content);
            Assert.AreEqual("secondport", view.SecondaryPort.Content);
            Assert.AreEqual("primaryhost", view.PrimaryHost.Content);
            Assert.AreEqual("primaryport", view.PrimaryPort.Content);
            Assert.AreEqual(1, view.Transport.Option);
            Assert.AreEqual(0, view.SyslogProtocol.Option);
            Assert.AreEqual(20, view.Facility.Option);
            Assert.AreEqual(0, view.Severity.Option);
            Assert.AreEqual("logone", logs.Names[0]);
            Assert.IsFalse(logs.IsChosen(0));
            Assert.AreEqual("logtwo", logs.Names[1]);
            Assert.IsTrue(logs.IsChosen(1));
            Assert.AreEqual("Agent service is happy", view.Status);
        }

        [TestMethod]
        public void ValidatesInputBeforeSaving() {
            presenter.Load();
            view.CarriageReturn.Content = "x";
            view.LineFeed.Content = "456";
            view.PollInterval.Content = "-3";
            view.Tab.Content = "y";
            presenter.Save();
            Assert.IsFalse(((TestValidatedString)view.CarriageReturn).IsValid);
            Assert.IsFalse(((TestValidatedString)view.LineFeed).IsValid);
            Assert.IsFalse(((TestValidatedString)view.PollInterval).IsValid);
            Assert.IsFalse(((TestValidatedString)view.Tab).IsValid);

            Assert.AreEqual("Validation errors - data was not saved.", view.Message);
        }

        [TestMethod]
        public void SavesConfiguration() {
            presenter.Load();
            view.CarriageReturn.Content = "23";
            view.LineFeed.Content = "45";
            view.PollInterval.Content = "6789";
            view.Tab.Content = "101";
            view.LookUpAccount.IsSelected = false;
            view.IncludeKeyValuePairs.IsSelected = false;
            view.SendToSecondary.IsSelected = false;
            view.EventIdFilter.Content = "filterx";
            view.Suffix.Content = "suffixx";
            view.SecondaryHost.Content = "secondhostx";
            view.SecondaryPort.Content = "secondportx";
            view.PrimaryHost.Content = "primaryhostx";
            view.PrimaryPort.Content = "primaryportx";
            view.Transport.Option = 2;
            view.SyslogProtocol.Option = 1;
            view.Facility.Option = 19;
            view.Severity.Option = 1;
            logs.IsChosens[0] = true;

            presenter.Save();

            Assert.AreEqual(23, configurationModel.Configuration.CarriageReturn);
            Assert.AreEqual(45, configurationModel.Configuration.LineFeed);
            Assert.AreEqual(6789, configurationModel.Configuration.PollInterval);
            Assert.AreEqual(101, configurationModel.Configuration.Tab);
            Assert.IsFalse(configurationModel.Configuration.LookUpAccountIDs);
            Assert.IsFalse(configurationModel.Configuration.IncludeKeyValuePairs);
            Assert.IsFalse(configurationModel.Configuration.SendToSecondary);
            Assert.AreEqual("filterx", configurationModel.Configuration.EventIdFilter);
            Assert.AreEqual("suffixx", configurationModel.Configuration.Suffix);
            Assert.AreEqual("secondhostx", configurationModel.Configuration.SecondaryHost);
            Assert.AreEqual("secondportx", configurationModel.Configuration.SecondaryPort);
            Assert.AreEqual("primaryhostx", configurationModel.Configuration.PrimaryHost);
            Assert.AreEqual("primaryportx", configurationModel.Configuration.PrimaryPort);
            Assert.AreEqual(Transport.Tcp, configurationModel.Configuration.Transport);
            Assert.IsFalse(configurationModel.Configuration.UseRFC3164);
            Assert.AreEqual(19, configurationModel.Configuration.Facility);
            Assert.AreEqual(0, configurationModel.Configuration.Severity);
            Assert.IsTrue(configurationModel.Configuration.EventLogs[0].IsChosen);

            Assert.AreEqual("Data saved successfully.", view.Message);
        }


        [TestMethod]
        public void SelectsAllLogs() {
            presenter.Load();
            presenter.SetAllChosen(true);
            foreach (var isChosen in logs.IsChosens) {
                Assert.IsTrue(isChosen);
            }
        }

        [TestMethod]
        public void UnselectsAllLogs() {
            presenter.Load();
            presenter.SetAllChosen(false);
            foreach (var isChosen in logs.IsChosens) {
                Assert.IsFalse(isChosen);
            }
        }

        [TestMethod]
        public void RestartsService() {
            serviceModel.IsDone = false;
            serviceModel.Status = "happy";
            presenter.Restart();
            while (!serviceModel.IsDone) Thread.Sleep(1);
            Assert.AreEqual("Agent service is very happy", view.Status);
        }

        [TestInitialize]
        public void SetUp() {
            configurationModel = new TestConfigurationModel {
                Configuration = new Configuration {
                    CarriageReturn = 12,
                    LineFeed = 34,
                    PollInterval = 5678,
                    Tab = 90,
                    LookUpAccountIDs = true,
                    IncludeKeyValuePairs = true,
                    SendToSecondary = true,
                    EventIdFilter = "filter",
                    Suffix = "suffix",
                    SecondaryHost = "secondhost",
                    SecondaryPort = "secondport",
                    PrimaryHost = "primaryhost",
                    PrimaryPort = "primaryport",
                    Transport = Transport.UdpAfterPing,
                    UseRFC3164 = true,
                    Facility = 20,
                    Severity = 8
                }
            };
            configurationModel.Configuration.EventLogs.Add(new EventLogCandidate {DisplayName = "logone", IsChosen = false});
            var logTwo = new EventLogCandidate {
                DisplayName = "logtwo",
                IsChosen = true
            };
            configurationModel.Configuration.EventLogs.Add(logTwo);
            serviceModel = new TestServiceModel();
            logs = new TestSelectionList();
            view = new TestView {
                CarriageReturn = new TestValidatedString(),
                LineFeed = new TestValidatedString(),
                PollInterval = new TestValidatedString(),
                Tab = new TestValidatedString(),
                LookUpAccount = new TestOption(),
                IncludeKeyValuePairs = new TestOption(),
                SendToSecondary = new TestOption(),
                EventIdFilter = new TestString(),
                Suffix = new TestString(),
                SecondaryHost = new TestString(),
                SecondaryPort = new TestString(),
                PrimaryHost = new TestString(),
                PrimaryPort = new TestString(),
                Transport = new TestOptionList(),
                SyslogProtocol = new TestOptionList(),
                Facility = new TestOptionList(),
                Severity = new TestOptionList(),
                Logs = logs
            };
            presenter = new MainPresenter(view, configurationModel, serviceModel);
        }

        TestConfigurationModel configurationModel;
        TestServiceModel serviceModel;
        MainPresenter presenter;
        // TestView view;
        TestSelectionList logs;
    }
}
#endif