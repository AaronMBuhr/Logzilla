/* SyslogAgentConfig: configuring a syslog agent for Windows
Copyright © 2021 LogZilla Corp.
*/

using System;
using System.Net.Http;
using System.Net.Security;
using System.Security.Cryptography.X509Certificates;
using System.Threading.Tasks;

namespace SyslogAgent.Config
{

    public class HttpFetcher
    {
        private static readonly HttpClient clientWithoutTls;
        private static readonly HttpClient clientWithTls;

        static HttpFetcher()
        {
            clientWithoutTls = new HttpClient();
            var handlerWithTls  = new HttpClientHandler() {
                ServerCertificateCustomValidationCallback = ( sender, cert, chain, policyErrors ) => true
            };
            clientWithTls = new HttpClient( handlerWithTls );

        }

        public static async Task<string> GetAsync( string url, string authToken, bool useTls )
        {
            var client = useTls ? clientWithTls : clientWithoutTls;
            // Create HttpRequestMessage
            var request = new HttpRequestMessage( HttpMethod.Get, url );

            // Set up request headers
            if( authToken != null )
            {
                request.Headers.Clear();
                request.Headers.Accept.Add( 
                    new System.Net.Http.Headers.MediaTypeWithQualityHeaderValue( "application/json" ) );
                request.Headers.Add( "Authorization", "token " + authToken );
            }

            try
            {
                // Send GET request
                HttpResponseMessage response = await client.SendAsync( request ).ConfigureAwait( false );
                response.EnsureSuccessStatusCode();

                // Read the response content
                string responseBody = await response.Content.ReadAsStringAsync();
                return responseBody;
            }
            catch( HttpRequestException e )
            {
                //Console.WriteLine( "\nException Caught!" );
                //Console.WriteLine( "Message :{0} ", e.Message );
                return null;
            }
        }
        
        public string GetSynchronous(string url, string authToken, bool useTls)
        {
            try
            {
                // Call the asynchronous method and block until it completes
                var task = GetAsync( url, authToken, useTls );
                return task.GetAwaiter().GetResult(); // This will block until the task is complete
            }
            catch( AggregateException ae )
            {
                // Handle exceptions (if any) here
                foreach( var e in ae.Flatten().InnerExceptions )
                {
                    Console.WriteLine( $"Error during request: {e.Message}" );
                }
                return null;
            }

        }
    }

}
