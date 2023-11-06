using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

namespace SyslogAgent.Config
{
    public static class Communications
    {
        public static string TestTcpConnection( string hostName, bool useTls )
        {
            // Define the port number
            int portNumber = ( useTls ? int.Parse( SharedConstants.ConfigDefaults.PrimaryTlsPortS )
                : int.Parse( SharedConstants.ConfigDefaults.PrimaryPortS ) );

            // Guard clause for null or empty hostname argument
            if( string.IsNullOrEmpty( hostName ) )
                return "Host name must not be null or empty.";

            try
            {
                // Port number should not be out of the range 0 to 65535
                if( portNumber < 0 || portNumber > 65535 )
                    throw new ArgumentOutOfRangeException( nameof( portNumber ), "Port number is out of range." );

                using( var tcpClient = new TcpClient() )
                {
                    // Try to connect
                    tcpClient.Connect( hostName, portNumber );
                    // If the connection is successful, return null
                    return null;
                }
            }
            catch( SocketException ex )
            {
                // Return the detailed message regarding the socket error
                return $"{ex.SocketErrorCode}: {ex.Message}";
            }
            catch( Exception ex )
            {
                // Return a generic message for any other exception
                return ex.Message;
            }
        }

    }
}
