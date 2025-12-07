interface RequestOptions {
  params?: Record<string, string | number | boolean | undefined>
  headers?: Record<string, string>
  signal?: AbortSignal
  /** Number of retry attempts for transient failures. Default: 0 */
  retries?: number
  /** Base delay in ms between retries (uses exponential backoff). Default: 1000 */
  retryDelay?: number
}

/** Status codes that are safe to retry */
const RETRYABLE_STATUS_CODES = [408, 429, 500, 502, 503, 504]

/**
 * Sleep for the specified duration.
 */
function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

/**
 * API client for making HTTP requests to the backend.
 */
class ApiClient {
  private baseUrl: string

  constructor(baseUrl: string = '') {
    this.baseUrl = baseUrl
  }

  private buildUrl(url: string, params?: Record<string, string | number | boolean | undefined>): string {
    if (!params) return url

    const searchParams = new URLSearchParams()
    Object.entries(params).forEach(([key, value]) => {
      if (value !== undefined && value !== '') {
        searchParams.append(key, String(value))
      }
    })

    const queryString = searchParams.toString()
    return queryString ? `${url}?${queryString}` : url
  }

  private async handleResponse<T>(response: Response): Promise<T> {
    if (!response.ok) {
      const errorData = await response.json().catch(() => ({}))
      throw new Error(errorData.message || errorData.error || `Request failed with status ${response.status}`)
    }

    const text = await response.text()
    if (!text) {
      return {} as T
    }

    return JSON.parse(text)
  }

  private async request<T>(
    url: string,
    options: RequestInit = {},
    retries = 0,
    retryDelay = 1000
  ): Promise<T> {
    let lastError: Error | null = null

    for (let attempt = 0; attempt <= retries; attempt++) {
      try {
        const response = await fetch(`${this.baseUrl}${url}`, {
          ...options,
          headers: {
            'Content-Type': 'application/json',
            ...options.headers,
          },
        })

        // Check if we should retry this response
        if (!response.ok && RETRYABLE_STATUS_CODES.includes(response.status) && attempt < retries) {
          const delay = retryDelay * Math.pow(2, attempt)
          await sleep(delay)
          continue
        }

        return this.handleResponse<T>(response)
      } catch (error) {
        lastError = error instanceof Error ? error : new Error(String(error))

        // Retry on network errors
        if (attempt < retries) {
          const delay = retryDelay * Math.pow(2, attempt)
          await sleep(delay)
          continue
        }
      }
    }

    throw lastError ?? new Error('Request failed after retries')
  }

  async get<T>(url: string, options?: RequestOptions): Promise<T> {
    const fullUrl = this.buildUrl(url, options?.params)
    return this.request<T>(
      fullUrl,
      {
        method: 'GET',
        signal: options?.signal,
      },
      options?.retries ?? 0,
      options?.retryDelay ?? 1000
    )
  }

  async post<T>(url: string, data?: unknown, options?: RequestOptions): Promise<T> {
    const fullUrl = this.buildUrl(url, options?.params)

    // Handle FormData - browser sets Content-Type with boundary automatically
    if (data instanceof FormData) {
      const response = await fetch(`${this.baseUrl}${fullUrl}`, {
        method: 'POST',
        body: data,
        signal: options?.signal,
      })
      return this.handleResponse<T>(response)
    }

    // Handle ArrayBuffer (binary data)
    if (data instanceof ArrayBuffer) {
      const response = await fetch(`${this.baseUrl}${fullUrl}`, {
        method: 'POST',
        body: data,
        headers: options?.headers,
        signal: options?.signal,
      })
      return this.handleResponse<T>(response)
    }

    // Handle JSON data
    return this.request<T>(
      fullUrl,
      {
        method: 'POST',
        body: data ? JSON.stringify(data) : undefined,
        signal: options?.signal,
      },
      options?.retries ?? 0,
      options?.retryDelay ?? 1000
    )
  }

  async put<T>(url: string, data: unknown, options?: RequestOptions): Promise<T> {
    const fullUrl = this.buildUrl(url, options?.params)
    return this.request<T>(
      fullUrl,
      {
        method: 'PUT',
        body: JSON.stringify(data),
        signal: options?.signal,
      },
      options?.retries ?? 0,
      options?.retryDelay ?? 1000
    )
  }

  async delete<T>(url: string, options?: RequestOptions): Promise<T> {
    const fullUrl = this.buildUrl(url, options?.params)
    return this.request<T>(
      fullUrl,
      {
        method: 'DELETE',
        signal: options?.signal,
      },
      options?.retries ?? 0,
      options?.retryDelay ?? 1000
    )
  }

  async uploadFile<T>(url: string, formData: FormData, options?: RequestOptions): Promise<T> {
    const fullUrl = this.buildUrl(url, options?.params)
    const response = await fetch(`${this.baseUrl}${fullUrl}`, {
      method: 'POST',
      body: formData,
      signal: options?.signal,
    })
    return this.handleResponse<T>(response)
  }
}

export const api = new ApiClient(import.meta.env.VITE_API_BASE_URL || '')
