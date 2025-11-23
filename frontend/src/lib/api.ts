interface RequestOptions {
  params?: Record<string, string | number | boolean | undefined>
  headers?: Record<string, string>
}

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

  private async request<T>(url: string, options: RequestInit = {}): Promise<T> {
    const response = await fetch(`${this.baseUrl}${url}`, {
      ...options,
      headers: {
        'Content-Type': 'application/json',
        ...options.headers,
      },
    })

    if (!response.ok) {
      const errorData = await response.json().catch(() => ({}))
      throw new Error(errorData.message || errorData.error || `Request failed with status ${response.status}`)
    }

    // Handle empty responses
    const text = await response.text()
    if (!text) {
      return {} as T
    }

    return JSON.parse(text)
  }

  async get<T>(url: string, options?: RequestOptions): Promise<T> {
    const fullUrl = this.buildUrl(url, options?.params)
    return this.request<T>(fullUrl, { method: 'GET' })
  }

  async post<T>(url: string, data?: unknown, options?: RequestOptions): Promise<T> {
    // Handle FormData
    if (data instanceof FormData) {
      const fullUrl = this.buildUrl(url, options?.params)
      const response = await fetch(`${this.baseUrl}${fullUrl}`, {
        method: 'POST',
        body: data,
        // Don't set Content-Type header - browser will set it with boundary for FormData
      })

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

    // Handle ArrayBuffer (binary data)
    if (data instanceof ArrayBuffer) {
      const fullUrl = this.buildUrl(url, options?.params)
      const response = await fetch(`${this.baseUrl}${fullUrl}`, {
        method: 'POST',
        body: data,
        headers: options?.headers,
      })

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

    const fullUrl = this.buildUrl(url, options?.params)
    return this.request<T>(fullUrl, {
      method: 'POST',
      body: data ? JSON.stringify(data) : undefined,
    })
  }

  async put<T>(url: string, data: unknown): Promise<T> {
    return this.request<T>(url, {
      method: 'PUT',
      body: JSON.stringify(data),
    })
  }

  async delete<T>(url: string): Promise<T> {
    return this.request<T>(url, { method: 'DELETE' })
  }

  async uploadFile<T>(url: string, formData: FormData): Promise<T> {
    const response = await fetch(`${this.baseUrl}${url}`, {
      method: 'POST',
      body: formData,
      // Don't set Content-Type header - browser will set it with boundary for FormData
    })

    if (!response.ok) {
      const errorData = await response.json().catch(() => ({}))
      throw new Error(errorData.message || errorData.error || `Upload failed with status ${response.status}`)
    }

    const text = await response.text()
    if (!text) {
      return {} as T
    }

    return JSON.parse(text)
  }
}

export const api = new ApiClient()
