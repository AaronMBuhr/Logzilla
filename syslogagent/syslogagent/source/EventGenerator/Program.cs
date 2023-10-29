using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

using EventGenerator;

namespace EventGenerator
{
    class Program
    {
        static void Main(string[] args)
        {
#if ORIGINAL
            if (args.Length < 2)
            {
                Console.WriteLine("Syntax:");
                Console.WriteLine("EventGenerator <num_events> <msec_between>");
                return;
            }

            long num_events = long.Parse(args[0]);
            int msec_between = int.Parse(args[1]);

            var gen = new EventGenerator();

            Console.WriteLine("Sending events...");
            for (int en = 0; en < num_events; ++en)
            {
                string msg_addendum = "Event #" + en;
                if (en % 10000 == 0)
                {
                    Console.WriteLine(msg_addendum);
                }
                gen.WriteFakeEvent(" : " + msg_addendum);
                if (msec_between > 0)
                {
                    Thread.Sleep(msec_between);
                }
            }
            Console.WriteLine("Done sending events.");
#else
            EventLogCreator.CreateEventLog();
            var gen = new EventGenerator();
            Console.WriteLine("Sending event...");
            gen.WriteFakeEvent();
#endif
        }
    }
}
