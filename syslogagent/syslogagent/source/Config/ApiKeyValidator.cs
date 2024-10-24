using SyslogAgent.Config;
using SyslogAgent;
using System.Text.RegularExpressions;
using System;

public static class ApiKeyValidator
{
    public static string ValidateApiKey(
        bool required,
        bool useTls,
        IValidatedStringView host,
        IValidatedStringView apiKey,
        string pfxPath,
        string failureMsg)
    {
        if (!required)
            return null;

        // Validate API key format
        bool isValid = !string.IsNullOrWhiteSpace(apiKey.Content) &&
            Regex.IsMatch(apiKey.Content.Trim(), @"^[a-zA-Z0-9]{48}$");

        if (!isValid)
        {
            return failureMsg;
        }

        // Normalize URL
        string url = NormalizeUrl(host.Content, useTls);

        try
        {
            using (var fetcher = new HttpFetcher(useTls ? pfxPath : null))
            {
                var (success, response, error) =
                    fetcher.GetSynchronous(url + SharedConstants.ApiPath, apiKey.Content);

                if (!success)
                {
                    Console.WriteLine($"API validation failed: {error}");
                    return failureMsg;
                }

                return null; // Success
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"API validation error: {ex.Message}");
            return failureMsg;
        }
    }

    private static string NormalizeUrl(string url, bool useTls)
    {
        url = url.Trim();
        if (!url.Contains("://"))
        {
            url = (useTls ? "https://" : "http://") + url;
        }
        return url;
    }
}

