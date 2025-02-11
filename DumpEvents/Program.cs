using System;
using System.Diagnostics.Eventing.Reader;
using System.Collections.Generic;
using System.Linq;
using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

class Program
{
    public class EventDataItem
    {
        public string? Name { get; set; }
        public string? Value { get; set; }
    }

    public class EventLogEntry
    {
        public string? Provider { get; set; }
        public int EventId { get; set; }
        public DateTime TimeCreated { get; set; }
        public string? Channel { get; set; }
        public string? Computer { get; set; }
        public string? SecurityUserId { get; set; }
        public string? Message { get; set; }
        public List<EventDataItem>? EventData { get; set; }
    }

    static void Main(string[] args)
    {
        if (args.Length < 3 || args.Length > 4)
        {
            Console.WriteLine("Usage: program.exe logName startDate endDate [eventId1,eventId2,...]");
            Console.WriteLine("Dates should be in format yyyy-MM-dd");
            Console.WriteLine("eventIds is an optional comma-separated list of event IDs to include");
            return;
        }

        string logName = args[0];
        if (!DateTime.TryParse(args[1], out DateTime startDate) ||
            !DateTime.TryParse(args[2], out DateTime endDate))
        {
            Console.WriteLine("Invalid date format. Use yyyy-MM-dd");
            return;
        }

        HashSet<int>? eventIds = null;
        if (args.Length == 4 && !string.IsNullOrWhiteSpace(args[3]))
        {
            eventIds = new HashSet<int>(
                args[3].Split(',')
                    .Select(s => int.TryParse(s.Trim(), out int id) ? id : -1)
                    .Where(id => id != -1)
            );
            if (!eventIds.Any())
            {
                Console.WriteLine("No valid event IDs provided, outputting all events");
                eventIds = null;
            }
        }

        try
        {
            var startUtc = startDate.ToUniversalTime();
            var endUtc = endDate.ToUniversalTime();
            var query = $"*[System[(TimeCreated[@SystemTime >= '{startUtc:yyyy-MM-ddTHH:mm:ss.000Z}' and @SystemTime <= '{endUtc:yyyy-MM-ddTHH:mm:ss.999Z}'])";
            if (eventIds != null)
            {
                query += $" and ({string.Join(" or ", eventIds.Select(id => $"EventID={id}"))})";
            }
            query += "]]";

            Console.WriteLine($"Using query: {query}");  // Debug output

            var eventLog = new EventLogReader(new EventLogQuery(logName, PathType.LogName, query));
            var events = new List<EventLogEntry>();
            EventRecord? record;

            while ((record = eventLog.ReadEvent()) != null)
            {
                using (record)
                {
                    var entry = new EventLogEntry
                    {
                        Provider = record.ProviderName,
                        EventId = record.Id,
                        TimeCreated = record.TimeCreated?.ToLocalTime() ?? DateTime.MinValue,
                        Channel = record.LogName,
                        Computer = record.MachineName,
                        SecurityUserId = record.UserId?.Value,
                        Message = record.FormatDescription(),
                        EventData = new List<EventDataItem>()
                    };

                    if (record.Properties != null)
                    {
                        for (int i = 0; i < record.Properties.Count; i++)
                        {
                            var propertyValue = record.Properties[i]?.Value?.ToString() ?? "";
                            entry.EventData.Add(new EventDataItem
                            {
                                Name = $"Data{i}",
                                Value = propertyValue
                            });
                        }
                    }
                    events.Add(entry);
                }
            }

            var serializer = new SerializerBuilder()
                .WithNamingConvention(CamelCaseNamingConvention.Instance)
                .Build();
            var yaml = serializer.Serialize(events);
            Console.WriteLine(yaml);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Error: {ex.Message}");
            if (ex is UnauthorizedAccessException)
            {
                Console.WriteLine("Note: You may need to run this program as Administrator to access the event logs.");
            }
        }
    }
}