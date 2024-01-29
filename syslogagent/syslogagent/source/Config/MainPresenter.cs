/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

using Newtonsoft.Json;
using System;
using System.IO;
using System.Diagnostics;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Controls;
using System.Globalization;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace SyslogAgent.Config
{
    public class MainPresenter
    {
        protected Registry registry_;
        protected Configuration config_;
        public MainPresenter(IMainView view, Registry registry, Configuration configuration, ServiceModel serviceModel)
        {
            this.view = view;
            this.registry_ = registry;
            this.config_ = configuration;
            registry_.ReadConfigFromRegistry(ref this.config_);
            this.serviceModel = serviceModel;
            this.eventLogTreeviewRoot = BuildTreeviewFromEventPaths(configuration.AllEventLogPaths);
            this.eventLogTreeviewRoot.SetIsCheckedAll(false);
            CheckEventPaths(this.eventLogTreeviewRoot, configuration.SelectedEventLogPaths);
        }

        public string GetLogzillaFileVersion()
        {
            try
            {
                FileVersionInfo verInfo = FileVersionInfo.GetVersionInfo(Globals.ExeFilePath + SharedConstants.SyslogAgentExeFilename);
                return verInfo.ProductVersion;
            }
            catch
            {
                return "not available";
            }
        }

        public void AddTreeviewItemPath(string leaf_path, EventLogTreeviewItem parent, IList<string> path_parts)
        {
            if (path_parts.Count < 1)
                return;
            List<string> remaining_parts = new List<string>(path_parts.Skip(1));
            EventLogTreeviewItem cur_node = null;
            foreach (var child in parent.Children)
            {
                if (child.Name == path_parts[0])
                {
                    cur_node = child;
                    break;
                }
            }
            if (cur_node == null)
            {
                cur_node = parent.AddChild(path_parts[0]);
            }
            if (remaining_parts.Count > 0)
            {
                AddTreeviewItemPath(leaf_path, cur_node, remaining_parts);
            }
            else
            {
                cur_node.LeafPath = leaf_path;
            }
        }

        public EventLogTreeviewItem BuildTreeviewFromEventPaths(IEnumerable<string> path_names)
        {
            var root = new EventLogTreeviewItem() { Name = "(root)" };
            foreach (var path in path_names)
            {
                var slash_parts = path.Split('/');
                var key_parts = new List<string>(slash_parts[0].Split('-'));
                if (slash_parts.Length > 1)
                {
                    key_parts.Add(slash_parts[1]);
                }
                AddTreeviewItemPath(path, root, key_parts);
            }
            return root;
        }

        public void CheckEventPath(EventLogTreeviewItem parent, IList<string> path_parts)
        {
            if (path_parts.Count < 1)
                return;
            List<string> remaining_parts = new List<string>(path_parts.Skip(1));
            foreach (var child in parent.Children)
            {
                if (child.Name == path_parts[0])
                {
                    if (remaining_parts.Count == 0)
                    {
                        child.IsChecked = true;
                    }
                    else
                    {
                        CheckEventPath(child, remaining_parts);
                    }
                    break;
                }
            }
        }

        public void CheckEventPaths(EventLogTreeviewItem root, IEnumerable<string> path_names)
        {
            foreach (var path in path_names)
            {
                var slash_parts = path.Split('/');
                var key_parts = new List<string>(slash_parts[0].Split('-'));
                if (slash_parts.Length > 1)
                {
                    key_parts.Add(slash_parts[1]);
                }
                CheckEventPath(root, key_parts);
            }
        }

        public void CheckAllEventPaths( EventLogTreeviewItem parent )
        {
            parent.IsChecked = true;
            foreach( var child in parent.Children )
            {
                CheckAllEventPaths( child );
            }
        }

        public void RecheckEventPaths(IEnumerable<string> path_names) {
            this.eventLogTreeviewRoot.SetIsCheckedAll(false);
            CheckEventPaths(this.eventLogTreeviewRoot, path_names);
        }

        public IEnumerable<string> GetSelectedLogPaths(EventLogTreeviewItem node)
        {
            if (node.Children == null || node.Children.Count == 0)
            {
                if (node.IsChecked == true)
                {
                    yield return node.LeafPath;
                }
            }
            else
            {
                foreach (var child in node.Children)
                {
                    foreach (var leaf in GetSelectedLogPaths(child))
                    {
                        yield return leaf;
                    }
                }
            }
        }

        public void Load()
        {
            registry_.ReadConfigFromRegistry(ref config_);
            view.IncludeEventIds.IsSelected = config_.IncludeVsIgnoreEventIds;
            view.IgnoreEventIds.IsSelected = !config_.IncludeVsIgnoreEventIds;
            view.OnlyWhileRunning.IsSelected = config_.OnlyWhileRunning;
            view.CatchUp.IsSelected = !config_.OnlyWhileRunning;
            view.EventIdFilter.Content = config_.EventIdFilter;
            view.Suffix.Content = config_.Suffix;
            view.Facility.Option = config_.Facility;
            view.LookUpAccount.IsSelected = config_.LookUpAccountIDs;
            //view.IncludeKeyValuePairs.IsSelected = config.IncludeKeyValuePairs;
            // view.PollInterval.Content = config.PollInterval.ToString();
            view.PrimaryHost.Content = config_.PrimaryHost;
            view.PrimaryApiKey.Content = config_.PrimaryApiKey;
            view.SendToSecondary.IsSelected = config_.SendToSecondary;
            view.SecondaryHost.Content = config_.SecondaryHost;
            view.SecondaryApiKey.Content = config_.SecondaryApiKey;
            view.Severity.Option = (config_.Severity + 1) % 9;
            view.Facility.Option = config_.Facility % 24;
            view.DebugLevel.Option = config_.DebugLevel % 9;
            view.PrimaryUseTls.IsSelected = config_.PrimaryUseTls;
            view.SecondaryUseTls.IsSelected = config_.SecondaryUseTls;
            view.DebugLevel.Option = config_.DebugLevel;
            view.DebugLogFilename.Content = config_.DebugLogFilename;
            view.TailFilename.Content = config_.TailFilename;
            view.TailProgramName.Content = config_.TailProgramName;
            view.LogzillaFileVersion = GetLogzillaFileVersion();

            //foreach (var log in config.EventLogs) view.Logs.Add(log.DisplayName, log.IsChosen);

            SetServiceStatus(serviceModel.Status);

        }

        public void SetAllChosen(bool isChosen)
        {
            CheckAllEventPaths( this.eventLogTreeviewRoot );
        }

        Configuration LoadConfigurationFromView()
        {
            var config = new Configuration();

            config.IncludeVsIgnoreEventIds = view.IncludeEventIds.IsSelected;
            config.OnlyWhileRunning = view.OnlyWhileRunning.IsSelected;
            config.EventIdFilter = view.EventIdFilter.Content;
            config.Suffix = view.Suffix.Content;
            config.Facility = view.Facility.Option;
            // config.PollInterval = Convert.ToInt32(view.PollInterval.Content);
            config.LookUpAccountIDs = view.LookUpAccount.IsSelected;
            //config.IncludeKeyValuePairs = view.IncludeKeyValuePairs.IsSelected;
            config.PrimaryHost = view.PrimaryHost.Content;
            config.PrimaryApiKey = view.PrimaryApiKey.Content;
            config.SecondaryHost = view.SecondaryHost.Content;
            config.SecondaryApiKey = view.SecondaryApiKey.Content;
            config.SendToSecondary = view.SendToSecondary.IsSelected;
            config.PrimaryUseTls = view.PrimaryUseTls.IsSelected;
            config.SecondaryUseTls = view.SecondaryUseTls.IsSelected;
            config.Severity = (view.Severity.Option + 8) % 9;
            config.DebugLevel = view.DebugLevel.Option;
            config.DebugLogFilename = view.DebugLogFilename.Content;
            config.TailFilename = view.TailFilename.Content;
            config.TailProgramName = view.TailProgramName.Content;

            var selected_logs = GetSelectedLogPaths(this.eventLogTreeviewRoot);
            config.SelectedEventLogPaths = selected_logs;

            return config;
        }

        public void Save()
        {
            if (!Validate())
            {
                return;
            }
            config_ = LoadConfigurationFromView();
            registry_.WriteConfigToRegistry(config_);
            view.SetSuccessMessage("Data saved successfully.");
        }

        public void Import()
        {
            try
            {
                string import_file_name = view.ChooseImportFileButton();
                if ((import_file_name ?? "") == "") { 
                    view.SetFailureMessage("Import aborted.");
                    return;
                }
                Registry.ReadRegistryImportFile(ref config_, import_file_name);
                view.UpdateDisplay(config_);
                view.SetSuccessMessage("Configuration imported");
            } catch (Exception ex) {
                view.SetFailureMessage("Configuration error: " + ex.Message);
            }
        }

        public void Export()
        {
            if (!Validate())
            {
                return;
            }
            string export_file_name = view.ChooseExportFileButton();
            if ((export_file_name ?? "") == "")
            {
                view.SetFailureMessage("Export Aborted");
                return;
            }
            Configuration config = LoadConfigurationFromView();
            Registry.CreateExportFileFromConfig(config, export_file_name);
            view.SetSuccessMessage("Export file created.");
        }

        public void PreviewInput()
        {
            view.Message = string.Empty;
        }

        public void Restart()
        {
            new Thread(() =>
            {
                serviceModel.Restart(SetServiceStatus);
            }).Start();
        }

        void SetServiceStatus(string status)
        {
            view.Status = "Agent service is " + status;
        }

        bool Validate()
        {

            var validationFunctions = new List<Func<string>>
            {
                 //() => ValidateInternetHost(view.PrimaryHost, true, "Invalid primary host"),
                 //() => ValidateHostConnectivity(view.PrimaryHost, view.PrimaryUseTls, true, "Primary host"),
                 //() => ValidateInternetHost(view.SecondaryHost, view.SendToSecondary.IsSelected, "Invalid secondary host"),
                 //() => ValidateHostConnectivity(view.SecondaryHost, view.SecondaryUseTls, view.SendToSecondary.IsSelected, "Secondary host"),
                 /* () => ValidateInterval(view.PollInterval, "Invalid poll interval"), */
                 () => ValidateIgnoreVsIncludeEventIds(view.EventIdFilter, view.IncludeEventIds, view.IgnoreEventIds, "Select either \"Include\" or \"Ignore\" event ids"),
                 () => ValidateEventIds(view.EventIdFilter, "Invalid event id filter"),
                 () => ValidateFilename(view.DebugLogFilename, "Invalid debug log filename"),
                 () => ValidateFilename(view.TailFilename, "Invalid tail filename"),
                 () => ValidatedSuffix(view.Suffix, "Invalid JSON"),
                 () => ValidatePrimaryTLS(view.PrimaryUseTls, "Push \"Select Primary Cert\" to choose a certificate file"),
                 () => ValidateSecondaryTLS(view.SecondaryUseTls, "Push \"Select Secondary Cert\" to choose a certificate file"),
                 // () => ValidateEventLogs(view.Logs, "Select at least one event log"),
                 () => ValidateTailProgramName(view.TailProgramName, view.TailFilename.Content, "Set a short program name for the tail log messages")
            };

            foreach (var fn in validationFunctions)
            {
                string msg = null;
                if ((msg = fn()) != null)
                {
                    view.SetFailureMessage(msg);
                    return false;
                }
            }

            return true;

        }

        static string ValidateInterval(IValidatedStringView interval, string failureMsg)
        {
            int result;
            var isValid = int.TryParse(interval.Content, out result);
            isValid &= result > 0;
            interval.IsValid = isValid;
            return isValid ? null : failureMsg;
        }

        static string ValidateFilename(IValidatedStringView filename, string failureMsg)
        {
            bool isValid = filename.IsValid = filename.Content.Trim().Length < 1 || Regex.Match(filename.Content, @"^[\:\\\w\-. ]+$").Success;
            return isValid ? null : failureMsg;
        }

        static string ValidateStringLength(IValidatedStringView value, int minLen, int maxLen, string failureMsg)
        {
            bool isValid = value.IsValid = !(value.Content.Length < minLen || value.Content.Length > maxLen);
            return isValid ? null : failureMsg;
        }

        static string ValidateInternetHost(IValidatedStringView host, bool required, string failureMsg)
        {
            var regex_valid_ip = @"^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$";
            var regex_valid_host = @"^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9])\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\-]*[A-Za-z0-9])$";
            bool isValid;
            if (host.Content.Trim() == "")
                isValid = host.IsValid = !required;
            else
            {
                isValid = host.IsValid = Regex.Match(host.Content, regex_valid_ip).Success
                    || Regex.Match(host.Content, regex_valid_host).Success;
            }
            return isValid ? null : failureMsg;
        }

        static string ValidateHostConnectivity(IValidatedStringView host, IValidatedOptionView useTls, bool required, string failureMsg)
        {
            if (!required && host.Content.Trim() == "")
                return null;
            string errMsg = Communications.TestTcpConnection(host.Content, useTls.IsSelected);
            return (errMsg == null ? null : $"{failureMsg} {errMsg}");
        }

        static string ValidateIgnoreVsIncludeEventIds( IValidatedStringView eventIds, IValidatedOptionView includeEventIds, IValidatedOptionView ignoreEventIds, string failureMsg )
        {
            bool isValid = eventIds.Content.Trim().Length < 1 || (includeEventIds.IsSelected ^ ignoreEventIds.IsSelected);
            return isValid ? null : failureMsg;
        }

        static string ValidateEventIds(IValidatedStringView eventIds, string failureMsg)
        {
            bool isValid = eventIds.IsValid = Regex.Match(eventIds.Content, @"^([0-9]{1,5},)*([0-9]{1,5})?$").Success;
            return isValid ? null : failureMsg;
        }

        static string ValidatedSuffix(IValidatedStringView suffix, string failureMsg)
        {
            suffix.IsValid = true;
            if (suffix.Content.Trim().Length > 0)
            {
                try
                {
                    dynamic deser = JsonConvert.DeserializeObject("{" + suffix.Content + "}");
                }
                catch (Exception ex)
                {
                    suffix.IsValid = false;
                    return "Invalid JSON body: " + ex.Message;
                }
            }
            suffix.IsValid = true;
            return null;
        }

        static string ValidatePrimaryTLS(IValidatedOptionView useTLS, string failureMsg)
        {
            bool isValid =
                (!useTLS.IsSelected)
                || (File.Exists(Globals.ExeFilePath + SharedConstants.PrimaryCertFilename));
            useTLS.IsValid = isValid;
            return isValid ? null : failureMsg;
        }

        static string ValidateSecondaryTLS(IValidatedOptionView useTLS, string failureMsg)
        {
            bool isValid =
                (!useTLS.IsSelected)
                || (File.Exists(Globals.ExeFilePath + SharedConstants.SecondaryCertFilename));
            useTLS.IsValid = isValid;
            return isValid ? null : failureMsg;
        }

        static string ValidateEventLogs(ISelectionListView logs, string failureMsg)
        {
            for (int i = 0; i < logs.Count; ++i)
            {
                if (logs.IsChosen(i))
                    return null;
            }
            return failureMsg;
        }

        static string ValidateTailProgramName(IValidatedStringView tailProgram, string tailFilename, string failureMsg)
        {
            bool isValid = tailProgram.IsValid 
                = (string.IsNullOrEmpty(tailFilename) ? true : tailProgram.Content.Trim() != string.Empty);
            return isValid ? null : failureMsg;
        }

        readonly IMainView view;
        readonly ServiceModel serviceModel;
        public readonly EventLogTreeviewItem eventLogTreeviewRoot;

        // Configuration config;
    }
}

